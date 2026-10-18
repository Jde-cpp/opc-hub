//The routed pages, for app.routes.ts' loadComponent - loaders, not the page classes, for the reason jde-opc's opc-pages.ts gives
//(#351):  a dynamic import of a barrel the site also imports statically splits nothing off.  QLList stays exported as well:  jde-opc's
//gateway page embeds it by name, so it loads with whichever of the two routes comes first.
export const loadCards = ()=>import( './cards/cards' ).then( m=>m.Cards );
export const loadLoginPage = ()=>import( './authorization/login-page/login-page' ).then( m=>m.LoginPage );
export const loadQLList = ()=>import( './ql/list/ql-list' ).then( m=>m.QLList );
export const loadApps = ()=>import( './apps/apps' ).then( m=>m.Apps );
export const loadAppServerDetail = ()=>import( './apps/app-server-detail/app-server-detail' ).then( m=>m.AppServerDetail );