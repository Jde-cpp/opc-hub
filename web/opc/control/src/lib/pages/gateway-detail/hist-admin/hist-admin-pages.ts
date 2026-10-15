//The template and group pages (#337) for app.routes.ts' loadComponent.  The routes can't import('jde-opc'):  the site imports that
//barrel statically as well, so it and every page it exports sit in the initial bundle and the dynamic import splits nothing off.
//The barrel exports these loaders instead of the pages, so the pages and what only they use load with the route.
export const loadHistTemplateDetail = ()=>import( './hist-template-detail' ).then( m=>m.HistTemplateDetail );
export const loadHistGroupDetail = ()=>import( './hist-group-detail' ).then( m=>m.HistGroupDetail );