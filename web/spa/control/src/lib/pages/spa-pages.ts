//The routed pages, as loaders for app.routes.ts' loadComponent (web/CLAUDE.md, "Routed pages are loaders").
export const loadComponentSidenav = ()=>import( './component-sidenav/component-sidenav' ).then( m=>m.ComponentSidenav );
export const loadHelpPage = ()=>import( './help/help-page' ).then( m=>m.HelpPage );
export const loadNotFound = ()=>import( './not-found/not-found' ).then( m=>m.NotFound );