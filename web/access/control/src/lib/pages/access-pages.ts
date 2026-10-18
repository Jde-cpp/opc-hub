//The routed pages, for app.routes.ts' loadComponent - loaders, not the page classes, for the reason jde-opc's opc-pages.ts gives
//(#351):  a dynamic import of a barrel the site also imports statically splits nothing off.  The lists' tableSettings, which the
//routes need by name, sit in their own files beside the pages.
export const loadUserDetail = ()=>import( './users/user-detail' ).then( m=>m.UserDetail );
export const loadGroupDetail = ()=>import( './groups/group-detail' ).then( m=>m.GroupDetail );
export const loadRoleDetail = ()=>import( './roles/role-detail' ).then( m=>m.RoleDetail );
export const loadResourceDetail = ()=>import( './resources/resource-detail' ).then( m=>m.ResourceDetail );