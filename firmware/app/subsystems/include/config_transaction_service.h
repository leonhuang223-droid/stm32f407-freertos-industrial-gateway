#ifndef GATEWAY_CONFIG_TRANSACTION_SERVICE_H
#define GATEWAY_CONFIG_TRANSACTION_SERVICE_H

#include "config_subsystem.h"
#include "alarm_subsystem.h"

typedef status_t (*config_persist_fn)(
    void *context, const gateway_storage_config_request_t *request);

/* Called under the config mutex: acquisition must not observe an uncommitted
 * alarm configuration. Storage is injected so failed persistence is testable.
 */
status_t
config_transaction_execute(config_subsystem_t *config,
                           alarm_subsystem_t *alarm,
                           const gateway_storage_config_request_t *request,
                           config_persist_fn persist,
                           void *persist_context);

#endif
