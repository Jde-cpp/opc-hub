# OpcGateway

Rest/Websocket Application on top of [open62541.org](https://www.open62541.org/): the gateway role - OPC UA client
sessions, browse/read/write/subscribe over REST and a websocket, the server connections and the OPC login - which
[`Jde.Opc.Hub`](../OpcHub/README.md) runs in one process with the AppServer.  Installing and running the product is the
hub's README (its "Install" section).  The standalone `Jde.AppServer` + `Jde.OpcGateway` pair (`apps/AppServer`, this
directory) still builds for split deployments but is not installed.
