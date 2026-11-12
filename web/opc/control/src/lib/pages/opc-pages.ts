//The routed pages, as loaders for app.routes.ts' loadComponent (web/CLAUDE.md, "Routed pages are loaders").
export const loadNodeDetail = ()=>import( './node-detail/node-detail' ).then( m=>m.NodeDetail );
export const loadGatewayDetail = ()=>import( './gateway-detail/gateway-detail' ).then( m=>m.GatewayDetail );
export const loadClientDetail = ()=>import( './gateway-detail/client-detail/client-detail' ).then( m=>m.ClientDetail );
export const loadOpcServerDetail = ()=>import( './opc-server-detail/opc-server-detail' ).then( m=>m.OpcServerDetail );
export const loadHistTemplateDetail = ()=>import( './gateway-detail/hist-admin/hist-template-detail' ).then( m=>m.HistTemplateDetail );
export const loadHistGroupDetail = ()=>import( './gateway-detail/hist-admin/hist-group-detail' ).then( m=>m.HistGroupDetail );
export const loadGatewayWatch = ()=>import( './gateway-detail/watch/gateway-watch' ).then( m=>m.GatewayWatch );
export const loadOeeMachineDetail = ()=>import( './gateway-detail/oee-admin/oee-machine-detail' ).then( m=>m.OeeMachineDetail );
export const loadOeeLineDetail = ()=>import( './gateway-detail/oee-admin/oee-line-detail' ).then( m=>m.OeeLineDetail );
export const loadOeeReasonDetail = ()=>import( './gateway-detail/oee-admin/oee-reason-detail' ).then( m=>m.OeeReasonDetail );
export const loadOeeDashboard = ()=>import( './gateway-detail/oee/oee-dashboard' ).then( m=>m.OeeDashboard );
export const loadOeeMachinePage = ()=>import( './gateway-detail/oee/oee-machine-page' ).then( m=>m.OeeMachinePage );