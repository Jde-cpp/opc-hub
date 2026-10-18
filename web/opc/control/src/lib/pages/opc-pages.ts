//The routed pages, for app.routes.ts' loadComponent.  A route can't `import('jde-opc').then( m=>m.Page )`:  the site imports that
//barrel statically as well, so the dynamic import needs its whole namespace and splits nothing off - every page it exported sat in
//the initial bundle (#351).  The barrel exports these loaders instead of the pages, each importing its page's file by path, so a page
//and what only it uses load with its route.  What main needs by name from a page's neighbourhood (gatewayTableSettings) lives in a
//file of its own, since a file main imports lands in the initial bundle whole.
export const loadNodeDetail = ()=>import( './node-detail/node-detail' ).then( m=>m.NodeDetail );
export const loadGatewayDetail = ()=>import( './gateway-detail/gateway-detail' ).then( m=>m.GatewayDetail );
export const loadClientDetail = ()=>import( './gateway-detail/client-detail/client-detail' ).then( m=>m.ClientDetail );
export const loadOpcServerDetail = ()=>import( './opc-server-detail/opc-server-detail' ).then( m=>m.OpcServerDetail );
export const loadHistTemplateDetail = ()=>import( './gateway-detail/hist-admin/hist-template-detail' ).then( m=>m.HistTemplateDetail );
export const loadHistGroupDetail = ()=>import( './gateway-detail/hist-admin/hist-group-detail' ).then( m=>m.HistGroupDetail );