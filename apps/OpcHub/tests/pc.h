#include <gtest/gtest.h>

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