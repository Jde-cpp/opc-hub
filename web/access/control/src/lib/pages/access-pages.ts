//The routed pages, as loaders for app.routes.ts' loadComponent (web/CLAUDE.md, "Routed pages are loaders").
export const loadUserDetail = ()=>import( './users/user-detail' ).then( m=>m.UserDetail );
export const loadGroupDetail = ()=>import( './groups/group-detail' ).then( m=>m.GroupDetail );
export const loadRoleDetail = ()=>import( './roles/role-detail' ).then( m=>m.RoleDetail );
export const loadResourceDetail = ()=>import( './resources/resource-detail' ).then( m=>m.ResourceDetail );