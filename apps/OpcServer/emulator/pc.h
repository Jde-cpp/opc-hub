#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/server_pubsub.h>
#include <open62541/plugin/log_stdout.h>

#include <jde/fwk.h>
#include <jde/fwk/io/json.h>
#include <jde/opc/usings.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/NodeId.h>

DISABLE_WARNINGS
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include <jde/app/proto/App.FromClient.pb.h>
#include <jde/app/proto/App.FromServer.pb.h>
ENABLE_WARNINGS
