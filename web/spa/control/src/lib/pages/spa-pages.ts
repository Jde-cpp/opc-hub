//The routed pages, for app.routes.ts' loadComponent - loaders, not the page classes, for the reason jde-opc's opc-pages.ts gives
//(#351):  a dynamic import of a barrel the site also imports statically splits nothing off.  The sidenav sits under nearly every
//route, so its chunk arrives with the first of them and only the login page is spared it.
export const loadComponentSidenav = ()=>import( './component-sidenav/component-sidenav' ).then( m=>m.ComponentSidenav );
export const loadHelpPage = ()=>import( './help/help-page' ).then( m=>m.HelpPage );
export const loadNotFound = ()=>import( './not-found/not-found' ).then( m=>m.NotFound );