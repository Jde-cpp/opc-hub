import { HelpTopic } from 'jde-spa';

//jde-framework's pages, registered by the site under HELP_TOPICS.  The markdown ships from assets/help (web/CLAUDE.md).
//No routes:  a list has no url of its own, so the ? button keeps opening the section's topic, which links here.  Its own
//array so the site can register it beside Navigation, ahead of the sections - registration order is display order.
export const listHelpTopics:HelpTopic[] = [
	{ id: 'lists', title: 'Lists and records', summary: 'Views, filters and sorting; saving, deleting and restoring', icon: 'table_rows', url: 'assets/jde-framework/help/lists.md' }
];
//'calendars' outranks 'apps' and jde-opc's 'gateways' on a calendar's page by its one more literal:  under the application
//server's page, and under a hub's, which is its gateway's.
export const frameworkHelpTopics:HelpTopic[] = [
	{ id: 'apps', title: 'Applications', summary: 'Running services, their logs and log levels', icon: 'apps', url: 'assets/jde-framework/help/apps.md', routes: ['apps', 'apps/appServers/:instance'] },
	{ id: 'calendars', title: 'Shift calendars', summary: 'When a plant is planned to run - shifts, the production day and holidays', icon: 'calendar_month', url: 'assets/jde-framework/help/calendars.md', routes: ['apps/appServers/:instance/calendars', 'apps/gateways/:instance/calendars'] }
];