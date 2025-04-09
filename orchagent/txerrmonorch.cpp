#include <linux/if_ether.h>

#include "txerrmonorch.h"
#include "port.h"
#include "logger.h"
#include "sai_serialize.h"
#include "converter.h"

#include <unordered_map>
#include <utility>
#include <exception>
#include <vector>

extern PortsOrch *gPortsOrch;

TxErrMonOrch::TxErrMonOrch(DBConnector *cfgDbConnector) :
    Orch(cfgDbConnector, CFG_TX_ERR_TABLE_NAME)
{
    SWSS_LOG_ENTER();
    // Initialize the tables members
    auto countersDb = std::make_shared<DBConnector>("COUNTERS_DB", 0);
    m_countersTable = std::make_shared<Table>(countersDb.get(), COUNTERS_TABLE);
    auto stateDb = std::make_shared<DBConnector>("STATE_DB", 0);
    m_txErrStateTable = std::make_shared<Table>(stateDb.get(), STATE_TX_ERR_TABLE_NAME);

    // Initialize the config members and table
    m_period = DEFAULT_TX_ERR_CFG_PERIOD_IN_SEC;
    m_threshold = DEFAULT_TX_ERR_CFG_THRESHOLD;
    vector<FieldValueTuple> fvs;
    fvs.emplace_back(TX_ERR_FIELD_CFG_PERIOD, to_string(m_period));
    fvs.emplace_back(TX_ERR_FIELD_CFG_THRESHOLD, to_string(m_threshold));
    auto txErrCfgTable = std::make_shared<Table>(cfgDbConnector, CFG_TX_ERR_TABLE_NAME);
    txErrCfgTable->set(TX_ERR_FIELD_CFG, fvs);

    // Initialize and start the timer
    auto interv = timespec { .tv_sec = DEFAULT_TX_ERR_CFG_PERIOD_IN_SEC, .tv_nsec = 0 };
    m_timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(m_timer, this, TX_ERR_TIMER_NAME);
    Orch::addExecutor(executor);
    m_timer->start();
}

void TxErrMonOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();
    try {
        auto it = consumer.m_toSync.begin();
        while (it != consumer.m_toSync.end())
        {
            KeyOpFieldsValuesTuple t = it->second;
            string key = kfvKey(t);
            string op = kfvOp(t);
            vector<FieldValueTuple> fvs = kfvFieldsValues(t);
            if (key == TX_ERR_FIELD_CFG)
            {
                // If the config values are updated
                if (op == SET_COMMAND)
                {
                    for (const auto &fv : fvs)
                    {
                        if (fvField(fv) == TX_ERR_FIELD_CFG_PERIOD)
                        {
                            // If the period is updated, reset the timer with the new period
                            m_period = to_uint<uint32_t>(fvValue(fv));
                            auto interv = timespec { .tv_sec = m_period, .tv_nsec = 0 };
                            m_timer->setInterval(interv);
                            m_timer->reset();
                            SWSS_LOG_INFO("Update tx err monitoring period to %u seconds", m_period);
                        }
                        else if (fvField(fv) == TX_ERR_FIELD_CFG_THRESHOLD)
                        {
                            m_threshold = to_uint<uint32_t>(fvValue(fv));
                            SWSS_LOG_INFO("Update tx err monitoring threshold to %u", m_threshold);
                        }
                    }
                }
            }
            consumer.m_toSync.erase(it++);
        }
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to handle tx err monitor cfg update\n");
    }
}

void TxErrMonOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();
    fetchTxErrCounters();
    updatePortStates();
}

void TxErrMonOrch::fetchTxErrCounters() {
    SWSS_LOG_ENTER();
    try
    {
        const auto ports = gPortsOrch->getAllPorts();
        for (const auto &portNameAndObj : ports)
        {
            auto portName = portNameAndObj.first;
            auto portObj = portNameAndObj.second;
            string portOid = sai_serialize_object_id(portObj.m_port_id);
            string value;
            m_countersTable->hget(portOid, TX_ERR_COUNTER_NAME, value);
            // We found the tx err counter for the port
            if (!value.empty())
            {
                uint64_t newTxErrCount = to_uint<uint64_t>(value);
                uint64_t oldTxErrCount = m_portToLastTxErrCount[portName];
                m_portToLastTxErrCount[portName] = newTxErrCount;
                m_portStatesForUpdate[portName] = ((newTxErrCount - oldTxErrCount) > m_threshold) ? INVALID_STATE : VALID_STATE;
            }
        }
    }
    catch (const std::exception& e)
    {
        SWSS_LOG_ERROR("Failed in fetching tx err counters: %s", e.what());
    }
}

void TxErrMonOrch::updatePortStates() {
    SWSS_LOG_ENTER();
    try
    {
        for (const auto &portAndState : m_portStatesForUpdate)
        {
            auto portName = portAndState.first;
            auto state = portAndState.second;
            vector<FieldValueTuple> fvs;
            fvs.emplace_back(TX_ERR_FIELD_STATE, state);
            m_txErrStateTable->set(portName, fvs);
            SWSS_LOG_INFO("Update state for port %s to %s", portName.c_str(), state.c_str());
        }
    }
    catch (const std::exception& e)
    {
        SWSS_LOG_ERROR("Failed in updating port tx err state: %s", e.what());
    }
}
