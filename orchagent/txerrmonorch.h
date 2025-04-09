#ifndef TXERRMONORCH_H
#define TXERRMONORCH_H

#include "orch.h"
#include "portsorch.h"
#include "select.h"
#include "producerstatetable.h"
#include "observer.h"
#include "selectabletimer.h"
#include "table.h"
#include "timer.h"

#include <map>
#include <algorithm>
#include <tuple>
#include <inttypes.h>

extern "C" {
#include "sai.h"
}

#define TX_ERR_FIELD_CFG "config"
#define TX_ERR_FIELD_CFG_PERIOD "period"
#define TX_ERR_FIELD_CFG_THRESHOLD "threshold"
#define TX_ERR_FIELD_STATE "state"
#define TX_ERR_COUNTER_NAME "SAI_PORT_STAT_IF_OUT_ERRORS"
#define TX_ERR_TIMER_NAME "TX_ERR_MONITORING_TIMER"
#define VALID_STATE "OK"
#define INVALID_STATE "NOT OK"
#define DEFAULT_TX_ERR_CFG_PERIOD_IN_SEC 30
#define DEFAULT_TX_ERR_CFG_THRESHOLD 0

typedef std::unordered_map<std::string, uint64_t> PortToLastTxErrCountMap;
typedef std::unordered_map<std::string, std::string> PortToTxErrStatesMap;

class TxErrMonOrch : public Orch
{
public:
    TxErrMonOrch(DBConnector *db);

private:
    virtual void doTask(Consumer &consumer);
    virtual void doTask(SelectableTimer &timer);
    void fetchTxErrCounters();
    void updatePortStates();

    unsigned int m_threshold;
    unsigned int m_period;

    PortToLastTxErrCountMap m_portToLastTxErrCount;
    PortToTxErrStatesMap m_portStatesForUpdate;
    SelectableTimer *m_timer;

    std::shared_ptr<Table> m_countersTable;
    std::shared_ptr<Table> m_txErrStateTable;
};

#endif /* TXERRMONORCH_H */
