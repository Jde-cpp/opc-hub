#include <gtest/gtest.h>

#include <boost/lexical_cast.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/unordered/concurrent_flat_set.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_highlevel_async.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/log_stdout.h>

#include <jde/fwk.h>
#include <jde/fwk/io/json.h>
#include <jde/opc/usings.h>
#include <jde/opc/UAException.h>

DISABLE_WARNINGS
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include <jde/app/proto/App.FromClient.pb.h>
#include <jde/app/proto/App.FromServer.pb.h>
ENABLE_WARNINGS

#include "../../OpcGateway/src/usings.h"
#include "../../OpcGateway/src/types/ServerCnnctn.h"
#include "Opc.FromServer.pb.h"
#include "../../OpcGateway/tests/utils/helpers.h"
