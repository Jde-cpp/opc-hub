import { HelpTopic } from 'jde-spa';

//jde-framework's pages, registered by the site under HELP_TOPICS.  The markdown ships from assets/help (web/CLAUDE.md).
//No routes:  a list has no url of its own, so the ? button keeps opening the section's topic, which links here.  Its own
//array so the site can register it beside Navigation, ahead of the sections - registration order is display order.
export const listHelpTopics:HelpTopic[] = [
	{ id: 'lists', title: 'Lists and records', summary: 'Views, filters and sorting; saving, deleting and restoring', icon: 'table_rows', url: 'assets/jde-framework/help/lists.md' }
];
export const frameworkHelpTopics:HelpTopic[] = [
	{ id: 'apps', title: 'Applications', summary: 'Running services, their logs and log levels', icon: 'apps', url: 'assets/jde-framework/help/apps.md', routes: ['apps', 'apps/appServers/:instance'] }
];
