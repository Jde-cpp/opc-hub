import {Routes} from '@angular/router';

import{ CalendarResolver, DetailResolver, IGRAPHQL, QLListResolver, QLListRouteService, HomeRouteService, AppResolver, loadApps, loadAppServerDetail, loadCalendarDetail, loadCards, loadLoginPage, loadQLList } from 'jde-framework';
import { HelpRouteService, helpTopicResolver, IROUTE_SERVICE, loadComponentSidenav, loadHelpPage, loadNotFound } from 'jde-spa';
import { AccessService, AuthGuard, Group, groupTableSettings, loadGroupDetail, loadResourceDetail, loadRoleDetail, loadUserDetail, ResourceResolver, resourceTableSettings, Role, roleTableSettings, User, userTableSettings } from 'jde-access';
import{ ClientResolver, GatewayRouteService, gatewayTableSettings, GatewayCnnctnRouteService, HistAdminResolver, loadClientDetail, loadFlowDetail, loadGatewayDetail, loadGatewayWatch, loadHistGroupDetail, loadHistTemplateDetail, loadNodeDetail, loadOeeDashboard, loadOeeLineDetail, loadOeeMachineDetail, loadOeeMachinePage, loadOeeReasonDetail, loadOpcServerDetail, NodeResolver, GatewayResolver, WatchResolver } from 'jde-opc';

const accessProvider = { provide: IGRAPHQL, useExisting: AccessService };//route-scoped token, but aliases the single providedIn:'root' instance instead of constructing a per-route one
const qlListProvider = { provide: IROUTE_SERVICE, useClass: QLListRouteService };
const helpProvider = { provide: IROUTE_SERVICE, useClass: HelpRouteService };
//The shift calendars (#396), under the application server's page and under a hub's, which is its gateway's:  the list is a tab of
//the page, so the bare list url - Back and Cancel from a detail land on it - redirects there, as the historian's lists do.  The
//page and its resolver talk to AppService themselves, so the routes provide no IGRAPHQL.
const calendarRoutes:Routes = [
	{ path: 'calendars', pathMatch: 'full', redirectTo: '' },
	{
		path: 'calendars/:slug',
		loadComponent: loadCalendarDetail,
		providers: [ CalendarResolver ],
		canActivate: [AuthGuard],
		data: { summary: "Shift calendar" },
		resolve: { pageData: CalendarResolver },
		runGuardsAndResolvers: "paramsChange"
	}
];

//Pages load through each library's <lib>-pages.ts loaders, never `import('jde-…')`:  this file imports every barrel statically,
//so a dynamic import of one splits nothing off.  What else keeps a page in main:  web/CLAUDE.md, "Routed pages are loaders".

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
				providers: [ NodeResolver ],
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
				providers:[ GatewayResolver ],
				resolve: {data: GatewayResolver},
				canActivate: [AuthGuard],
				data: { tableSettings: gatewayTableSettings }
			},
			//The historian's templates and groups (#337):  their lists are tabs of the gateway's page, so the bare list urls - Back and
			//Cancel from a detail land on them - redirect to it, ahead of ':connection', which would take the segment for a connection.
			//A segment added ahead of ':connection' joins ServerCnnctn.reservedSlugs and ServerCnnctn::SlugError, which refuse it as a slug.
			{ path: 'templates', pathMatch: 'full', redirectTo: '' },
			{ path: 'groups', pathMatch: 'full', redirectTo: '' },
			{
				path: 'templates/:slug',
				loadComponent: loadHistTemplateDetail,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "History template", kind: "templates" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			{
				path: 'groups/:slug',
				loadComponent: loadHistGroupDetail,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "History group", kind: "groups" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			//OEE's machines and reasons (#404), tabs of the page as the historian's lists are, with the same resolver by their kind.
			{ path: 'machines', pathMatch: 'full', redirectTo: '' },
			{ path: 'reasons', pathMatch: 'full', redirectTo: '' },
			{
				path: 'machines/:slug',
				loadComponent: loadOeeMachineDetail,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "OEE machine", kind: "machines" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			{//a machine's figures, Gantt chart, losses, trend and stops (#405):  the machine's row by the same resolver, under its own page
				path: 'machines/:slug/oee',
				loadComponent: loadOeeMachinePage,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "OEE", kind: "machines", suffix: "oee" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			//OEE's lines (#409):  a line's page, and under it its OEE page, which is the machine's over the line's machines
			{ path: 'lines', pathMatch: 'full', redirectTo: '' },
			{
				path: 'lines/:slug',
				loadComponent: loadOeeLineDetail,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "OEE line", kind: "lines" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			{
				path: 'lines/:slug/oee',
				loadComponent: loadOeeMachinePage,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "OEE", kind: "lines", suffix: "oee" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			{
				path: 'reasons/:slug',
				loadComponent: loadOeeReasonDetail,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "OEE stop reason", kind: "reasons" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			//A flow's page (#455), under the Flows tab's list.  No bare `flows` redirect, as the lists above have:  `flows` is the flow
			//server's connection, the one row the gateway makes itself, and ':connection' serves its page, so a flow's page goes back to
			//the gateway's own (gatewayListLink).
			{
				path: 'flows/:slug',
				loadComponent: loadFlowDetail,
				providers: [ HistAdminResolver ],
				canActivate: [AuthGuard],
				data: { summary: "Flow", kind: "flows" },
				resolve: { pageData: HistAdminResolver },
				runGuardsAndResolvers: "paramsChange"
			},
			{//OEE's dashboard (#406):  a live tile per machine and the Gantt chart of the current shift, a page under the gateway's as the watch list is
				path: 'oee',
				loadComponent: loadOeeDashboard,
				providers: [ WatchResolver ],
				canActivate: [AuthGuard],
				data: { summary: "OEE dashboard" },
				resolve: { pageData: WatchResolver }
			},
			...calendarRoutes,//ahead of ':connection' too
			{//the watch list (#419):  rows from any of the gateway's connections, ahead of ':connection', which would take the segment for one
				path: 'watch',
				loadComponent: loadGatewayWatch,
				providers: [ WatchResolver ],
				canActivate: [AuthGuard],
				data: { summary: "Watch list" },
				resolve: { pageData: WatchResolver }
			},
			{
				path: ':connection',
				loadComponent: loadClientDetail,
				providers: [ ClientResolver ],
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
			},
			...calendarRoutes
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
			{ path: ':topic', loadComponent: loadHelpPage, resolve: { topic: helpTopicResolver }, runGuardsAndResolvers: "paramsChange" },
			{ path: ':topic/:subtopic', loadComponent: loadHelpPage, resolve: { topic: helpTopicResolver }, runGuardsAndResolvers: "paramsChange" }//a page split out of another's (HelpTopic.id)
		]
	},
	//last:  a url nothing above matches (a mistyped or stale bookmark) was a blank page (reviews/m3-closing.md #38).  data.notFound
	//keeps it out of the crumbs, tiles, favorites, search and Recently visited (jde-spa isNotFound).  No AuthGuard, like help.
	{ path: '**', title: "Page not found", loadComponent: loadNotFound, data: {notFound: true} }
];