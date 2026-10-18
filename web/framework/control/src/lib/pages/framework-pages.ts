//The routed pages, as loaders for app.routes.ts' loadComponent (web/CLAUDE.md, "Routed pages are loaders").
export const loadCards = ()=>import( './cards/cards' ).then( m=>m.Cards );
export const loadLoginPage = ()=>import( './authorization/login-page/login-page' ).then( m=>m.LoginPage );
export const loadQLList = ()=>import( './ql/list/ql-list' ).then( m=>m.QLList );
export const loadApps = ()=>import( './apps/apps' ).then( m=>m.Apps );
export const loadAppServerDetail = ()=>import( './apps/app-server-detail/app-server-detail' ).then( m=>m.AppServerDetail );