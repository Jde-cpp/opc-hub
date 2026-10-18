import {Routes} from '@angular/router';

import{ DetailResolver, IGRAPHQL, QLListResolver, QLListRouteService, HomeRouteService, AppResolver, loadApps, loadAppServerDetail, loadCards, loadLoginPage, loadQLList } from 'jde-framework';
import { HelpRouteService, helpTopicResolver, IROUTE_SERVICE, loadComponentSidenav, loadHelpPage, loadNotFound } from 'jde-spa';
import { AccessService, AuthGuard, Group, groupTableSettings, loadGroupDetail, loadResourceDetail, loadRoleDetail, loadUserDetail, ResourceResolver, resourceTableSettings, Role, roleTableSettings, User, userTableSettings } from 'jde-access';
import{ ClientResolver, GatewayRouteService, gatewayTableSettings, GatewayCnnctnRouteService,GatewayService, HistAdminResolver, loadClientDetail, loadGatewayDetail, loadHistGroupDetail, loadHistTemplateDetail, loadNodeDetail, loadOpcServerDetail, NodeResolver, OpcNodeRouteService, GatewayResolver } from 'jde-opc';

const accessProvider = { provide: IGRAPHQL, useExisting: AccessService };//route-scoped token, but aliases the single providedIn:'root' instance instead of constructing a per-route one
const gatewayProvider = { provide: IGRAPHQL, useExisting: GatewayService };//route-scoped token, but aliases the single providedIn:'root' instance instead of constructing a per-route one
const qlListProvider = { provide: IROUTE_SERVICE, useClass: QLListRouteService };
const opcNodeRouteProvider = { provide: IROUTE_SERVICE, useExisting: OpcNodeRouteService };//NodeChildren injects the class token, so the route binding must alias that instance rather than build a second one
const helpProvider = { provide: IROUTE_SERVICE, useClass: HelpRouteService };

//Pages are loadComponent through the loaders each library exports - opc-pages.ts, access-pages.ts, framework-pages.ts, spa-pages.ts -
//which import the page's file by path, and that is what splits it off.  `()=>import('jde-…').then( m=>m.Page )` split nothing:  this file
//imports every barrel statically, so a dynamic import of one needs the whole namespace, and every page sat in the initial bundle - 1.99 MB
//against the 2 MB budget, 1.11 MB with the loaders (#351).  A page stays split only while nothing in the initial bundle imports its file:
//what this file or a resolver needs by name (a tableSettings constant, HomeRouteService, the hist lists' route) sits in a file of its
//own beside the page, and a barrel export of an unused component can hold its file in main by itself (NodeChildren's did).  The sidenav
//and the cards are under nearly every route, so their chunks arrive with the first one;  only /login is spared them.

export const routes: Routes = [
	{ path: '', title: "Home", loadComponent: loadCards,
		data: {summary: "Welcome to OPC Hub", hero: {lead: "Welcome to", name: "OPC Hub", tagline: "Browse OPC UA servers, manage who can reach them, and monitor the services behind them.", tilesHeading: "Where do you want to begin?"} },
		canActivate: [AuthGuard],
		providers: [  {provide: IROUTE_SERVICE, useClass: HomeRouteService} ]},
	{ path: 'login', title: "Login", loadComponent: loadLoginPage, data: {name: "Login", summary: "Login to Site"} },
	{ path: 'gateways', title: "Gateways", canActivate: [AuthGuard], loadComponent: loadCards,
		providers: [{provide: IROUTE_SERVICE, useClass: GatewayRouteService}],
		data: {summary: "Available Gateways", icon: "hub"}
	},
	{ path: 'gateways/:gateway', title: ":gateway", canActivate: [AuthGuard], loadComponent: loadCards,
		providers: [{provide: IROUTE_SERVICE, useClass: GatewayCnnctnRouteService}],
		data: {summary: "Available Connections",}
	},
	{
		path: 'gateways/:gateway/:connection', loadComponent: loadComponentSidenav, canActivate: [AuthGuard],
		children :[
			{
				path: '**',
				loadComponent: loadNodeDetail,
				providers: [ NodeResolver, opcNodeRouteProvider ],
				canActivate: [AuthGuard],
				data: { summary: "Opc Gateway Detail", collectionName: "serverConnections" },
				resolve: { pageData: NodeResolver },
				runGuardsAndResolvers: "pathParamsOrQueryParamsChange"
			}
		]
	},
	{ path: 'access', title: "Access", loadComponent: loadCards, providers: [qlListProvider], canActivate: [AuthGuard], data: {
		summary: "Configure User Access", icon: "admin_panel_settings"
	} },
	{ path: 'access', loadComponent: loadComponentSidenav, canActivate: [AuthGuard], providers: [qlListProvider],
			children :[
				{ path: 'users/:slug',
					loadComponent: loadUserDetail,
					providers: [ DetailResolver<User>, accessProvider ],
					resolve: { pageData: DetailResolver<User> },
					canActivate: [AuthGuard],
					runGuardsAndResolvers: "paramsChange"
				},
				{ path: 'groups/:slug',
					loadComponent: loadGroupDetail,
					providers: [ DetailResolver<Group>, accessProvider ],
					resolve: { pageData: DetailResolver<Group> },
					canActivate: [AuthGuard],
					runGuardsAndResolvers: "paramsChange",
				},
				{ path: 'roles/:slug',
					loadComponent: loadRoleDetail,
					providers: [ DetailResolver<Role>, accessProvider ],
					data: { summary: "Role Detail" },
					resolve: { pageData: DetailResolver<Role> },
					canActivate: [AuthGuard],
					runGuardsAndResolvers: "paramsChange"
				},
				{ path: 'resources/:schema/:slug',//a slug repeats across schemas (`logs`) - ResourceResolver
					loadComponent: loadResourceDetail,
					providers: [ ResourceResolver, accessProvider ],
					data: { summary: "Resource Detail" },
					resolve: { pageData: ResourceResolver },
					canActivate: [AuthGuard],
					runGuardsAndResolvers: "paramsChange"
				},
				{ path: ':collectionDisplay',
					loadComponent: loadQLList,
					runGuardsAndResolvers: "paramsChange",
					providers: [ QLListResolver, accessProvider ],
					resolve: { data: QLListResolver },
					canActivate: [AuthGuard],
					data: { collections: [
						//summary:  the line under each card on /access (and its search result) - what the list is for
						{ path:"users", data:{tableSettings: userTableSettings, icon: "person", summary: "People and service identities"} },
						{ path:"groups", data:{tableSettings: groupTableSettings, icon: "group", summary: "Users gathered to grant roles once"} },
						{ path: "roles", data:{tableSettings: roleTableSettings, icon: "badge", summary: "Sets of rights"} },
						{ path:"resources", data:{tableSettings: resourceTableSettings, icon: "lock", summary: "What access rules apply to"} }
					]}
				},
			]
	},
	{
		path: 'apps',
		title: "Applications",
		canActivate: [AuthGuard],
		loadComponent: loadApps,
		providers: [ AppResolver, accessProvider ],
		resolve: { connections: AppResolver },
		data: { summary: "Running Services", icon: "apps" },
	},
	{
		path: 'apps/gateways/:instance', loadComponent: loadComponentSidenav, canActivate: [AuthGuard],
		children :[
			{
				path: '',
				loadComponent: loadGatewayDetail,
				providers:[ GatewayResolver, gatewayProvider],
				resolve: {data: GatewayResolver},
				canActivate: [AuthGuard],
				data: { tableSettings: gatewayTableSettings }
			},
			//The historian's templates and groups (#337):  their lists are tabs of the gateway's page, so the bare list urls - Back and
			//Cancel from a detail land on them - redirect to it, ahead of ':connection', which would take the segment for a connection.
			{ path: 'templates', pathMatch: 'full', redirectTo: '' },
			{ path: 'groups', pathMatch: 'full', redirectTo: '' },
			{
				path: 'templates/:slug',
				loadComponent: loadHistTemplateDetail,
				providers: [ HistAdminResolver, gatewayProvider ],
				canActivate: [AuthGuard],
				data: { summary: "History template", kind: "templates" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			{
				path: 'groups/:slug',
				loadComponent: loadHistGroupDetail,
				providers: [ HistAdminResolver, gatewayProvider ],
				canActivate: [AuthGuard],
				data: { summary: "History group", kind: "groups" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			{
				path: ':connection',
				loadComponent: loadClientDetail,
				providers: [ ClientResolver, gatewayProvider ],
				canActivate: [AuthGuard],
				data: { summary: "Opc Connection", collectionName: "serverConnections" },
				resolve: { pageData: ClientResolver }
			}
		]
	},
	{
		//AppResolver builds this url from the program name (Jde.AppServer -> appServers), so the segment must match it.
		path: 'apps/appServers/:instance', loadComponent: loadComponentSidenav, canActivate: [AuthGuard],
		children :[
			{
				path: '',
				loadComponent: loadAppServerDetail,
				canActivate: [AuthGuard],
				data: { summary: "Application Server" }
			}
		]
	},
	{
		//AppResolver builds this url from the program name (Jde.OpcServer -> opcServers), so the segment must match it.
		path: 'apps/opcServers/:instance', loadComponent: loadComponentSidenav, canActivate: [AuthGuard],
		children :[
			{
				path: '',
				loadComponent: loadOpcServerDetail,
				canActivate: [AuthGuard],
				data: { summary: "Opc Server" }
			}
		]
	},
	//the access idiom:  the cards route has no children, so /help/<topic> falls through to the sidenav one.  No AuthGuard on
	//purpose - help stays readable signed out, like login.
	{ path: 'help', title: "Help", loadComponent: loadCards, providers: [helpProvider], data: {summary: "Documentation", icon: "help_outline"} },
	{ path: 'help', loadComponent: loadComponentSidenav,
		children :[
			{ path: ':topic', loadComponent: loadHelpPage, resolve: { topic: helpTopicResolver }, runGuardsAndResolvers: "paramsChange" }
		]
	},
	//last:  a url nothing above matches (a mistyped or stale bookmark) was a blank page (reviews/m3-closing.md #38).  data.notFound
	//keeps it out of the crumbs, tiles, favorites, search and Recently visited (jde-spa isNotFound).  No AuthGuard, like help.
	{ path: '**', title: "Page not found", loadComponent: loadNotFound, data: {notFound: true} }
];