import { provideHttpClient } from "@angular/common/http";
import { ApplicationConfig } from '@angular/core';
import { MAT_TABS_CONFIG } from '@angular/material/tabs';
import { MAT_NATIVE_DATE_FORMATS, MatDateFormats, provideNativeDateAdapter } from '@angular/material/core';
import { provideRouter, TitleStrategy } from '@angular/router';
import { APP_SERVICE, AppCardStatus, AppService, AUTH_STORE, AuthStore, CARD_STATUS, frameworkHelpTopics, HelpCardStatus, ProfileService } from 'jde-framework'
import { GATEWAY_SERVICE, GatewayCardStatus, GatewayConnectionsStatus, GatewayService, nodeSegmentName, NodeSearchProvider, OPC_STORE, OpcAuthService, opcHelpTopics, OpcNodeLinkResolver, OpcStore} from 'jde-opc';
import { APP_LOGO, APP_NAME, AppTitleStrategy, HELP_LINKS, HELP_TOPICS, HelpLink, HelpTopic, IAUTH, IENVIRONMENT, IPROFILE_SERVICE, RouteSearchProvider, SEARCH_PROVIDERS, SEGMENT_NAME, spaHelpTopics } from 'jde-spa';
import {EnvironmentService} from './services/environment-service';
import { routes } from './app.routes';
import { ACCESS_SERVICE, AccessCardStatus, AccessCollectionStatus, accessHelpTopics, AccessSearchProvider, AccessService, NODE_LINK_RESOLVER } from "jde-access";

//the site's own help topics - the libraries export theirs.  Served from src/assets (web/opc/site/assets, linked by setup.sh).
const siteOverview:HelpTopic[] = [ {id: 'overview', title: 'Overview', summary: 'What this site is and where to start', icon: 'menu_book', url: 'assets/site/help/overview.md'} ];
const siteAbout:HelpTopic[] = [ {id: 'about', title: 'About', summary: 'Version and source', icon: 'info', url: 'assets/site/help/about.md'} ];
const siteLinks:HelpLink[] = [ {title: 'Report an issue', summary: 'Bugs and feature requests, on GitHub', icon: 'bug_report', href: 'https://github.com/Jde-cpp/opc-hub/issues'} ];

//2-digit rather than the native numeric:  it zero-pads the datepicker input ("08/27/2026", not "8/27/2026"), so a column
//of dates is one width and lines up when right-aligned.  These are Intl.DateTimeFormat OPTIONS, not a pattern string, so
//the field order stays the locale's - 08/27/2026 in en-US, 27/08/2026 in en-GB, 27.08.2026 in de-DE.  Only dateInput is
//overridden;  the a11y labels and the month/year header keep the native spelling.
const dateFormats:MatDateFormats = {
	...MAT_NATIVE_DATE_FORMATS,
	display: {...MAT_NATIVE_DATE_FORMATS.display, dateInput: {year: 'numeric', month: '2-digit', day: '2-digit'}}
};

export const appConfig: ApplicationConfig = {
  providers: [
		provideHttpClient(),
		provideRouter(routes),
		{provide: TitleStrategy, useExisting: AppTitleStrategy},//tabs read "<page> · OPC Hub"
		{provide: APP_NAME, useValue: 'OPC Hub'},//also the navbar's home link, with the mark below
		{provide: APP_LOGO, useValue: 'assets/site/logo.svg'},
		//every datepicker in the app takes its DateAdapter and formats from here - the components used to provide their own,
		//which is one adapter instance per component and as many places to edit.  One provider is also the single point to
		//swap the native adapter for a locale-aware one:  NativeDateAdapter.parse ignores the format and calls Date.parse, so
		//it reads en-US/ISO input and nothing else.  MAT_DATE_LOCALE is deliberately NOT provided - it already defaults to
		//LOCALE_ID, which is the one knob to set when the app is localised.
		provideNativeDateAdapter( dateFormats ),
		//0ms kills both tab animations at once:  MatTabGroup feeds animationDuration to --mat-tab-body-animation-duration (the body slide) and --mat-tab-header-animation-duration (the ink bar), and flags the group noopable.  dynamicHeight (the wrapper-height transition) is off by default and must stay off in the templates - an attribute there overrides this.
		{provide: MAT_TABS_CONFIG, useValue: {animationDuration: '0ms'}},
		{provide: ACCESS_SERVICE, useExisting: AccessService},
		{provide: APP_SERVICE, useExisting: AppService},
		{provide: IAUTH, useClass: OpcAuthService},
		{provide: AUTH_STORE, useClass: AuthStore},
		{provide: IENVIRONMENT, useClass: EnvironmentService},
		{provide: GATEWAY_SERVICE, useExisting: GatewayService},//useExisting, not useClass:  useClass is a construction recipe, so each token would build its own GatewayService (and its own sockets/queries)
		{provide: OPC_STORE, useExisting: OpcStore},//string-token writers (GatewayService, NodeResolver, NodeRoute) and class-token readers (ClientResolver) must share one store
		{provide: NODE_LINK_RESOLVER, useExisting: OpcNodeLinkResolver},//jde-access's Effective rights tab links a node-scoped resource to its node page; only jde-opc can place a node
		{provide: IPROFILE_SERVICE, useExisting: ProfileService},//ProfileStore (jde-spa) persists via this token; jde-spa can't import the framework implementation
		//the navbar search (jde-spa) fans out through this multi token, same reason;  registration order is result precedence.
		{provide: SEARCH_PROVIDERS, useExisting: RouteSearchProvider, multi: true},
		{provide: SEARCH_PROVIDERS, useExisting: AccessSearchProvider, multi: true},
		{provide: SEARCH_PROVIDERS, useExisting: NodeSearchProvider, multi: true},
		//the live figures on the home tiles, the section headers and the section cards - one per page url
		{provide: CARD_STATUS, useExisting: GatewayCardStatus, multi: true},
		{provide: CARD_STATUS, useExisting: GatewayConnectionsStatus, multi: true},
		{provide: CARD_STATUS, useExisting: AccessCardStatus, multi: true},
		{provide: CARD_STATUS, useExisting: AccessCollectionStatus, multi: true},
		{provide: CARD_STATUS, useExisting: AppCardStatus, multi: true},
		{provide: CARD_STATUS, useExisting: HelpCardStatus, multi: true},
		{provide: SEGMENT_NAME, useValue: nodeSegmentName},//node pages' breadcrumbs (and Recently visited) read pump1, not 5~pump1
		//the help section (jde-spa) lists these in registration order - the site's overview first, about last, the sections
		//between them in the navbar's order.
		{provide: HELP_TOPICS, useValue: siteOverview, multi: true},
		{provide: HELP_TOPICS, useValue: spaHelpTopics, multi: true},
		{provide: HELP_TOPICS, useValue: opcHelpTopics, multi: true},
		{provide: HELP_TOPICS, useValue: accessHelpTopics, multi: true},
		{provide: HELP_TOPICS, useValue: frameworkHelpTopics, multi: true},
		{provide: HELP_TOPICS, useValue: siteAbout, multi: true},
		//and after them on /help, the cards that leave the site
		{provide: HELP_LINKS, useValue: siteLinks, multi: true},
		//OpcNodeRouteService/AuthGuard need no string token - every consumer injects the class, which providedIn:'root' already supplies
	]
};