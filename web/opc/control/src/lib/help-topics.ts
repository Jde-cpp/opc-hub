import { HelpTopic } from 'jde-spa';

//jde-opc's pages, registered by the site under HELP_TOPICS.  The markdown ships from assets/help (web/CLAUDE.md).
//'gateways' covers the whole browse tree as a prefix; the two apps/ patterns outrank jde-framework's 'apps' by literal count, and
//the historian's template and group pages outrank 'gateways' by one more.  'gateways/groups' is split out of the gateways page.
export const opcHelpTopics:HelpTopic[] = [
	{ id: 'gateways', title: 'Gateways', summary: 'Gateways, server connections and node browsing', icon: 'hub', url: 'assets/jde-opc/help/gateways.md', routes: ['gateways', 'apps/gateways/:instance', 'apps/opcServers/:instance'] },
	{ id: 'gateways/groups', title: 'Historian groups', summary: 'Templates and groups - what the gateway historian keeps', icon: 'history', url: 'assets/jde-opc/help/groups.md', routes: ['apps/gateways/:instance/templates', 'apps/gateways/:instance/groups'] }
];
