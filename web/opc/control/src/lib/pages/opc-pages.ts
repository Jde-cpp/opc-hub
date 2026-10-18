//The routed pages, as loaders for app.routes.ts' loadComponent (web/CLAUDE.md, "Routed pages are loaders").
export const loadNodeDetail = ()=>import( './node-detail/node-detail' ).then( m=>m.NodeDetail );
export const loadGatewayDetail = ()=>import( './gateway-detail/gateway-detail' ).then( m=>m.GatewayDetail );
export const loadClientDetail = ()=>import( './gateway-detail/client-detail/client-detail' ).then( m=>m.ClientDetail );
export const loadOpcServerDetail = ()=>import( './opc-server-detail/opc-server-detail' ).then( m=>m.OpcServerDetail );
export const loadHistTemplateDetail = ()=>import( './gateway-detail/hist-admin/hist-template-detail' ).then( m=>m.HistTemplateDetail );
export const loadHistGroupDetail = ()=>import( './gateway-detail/hist-admin/hist-group-detail' ).then( m=>m.HistGroupDetail );