import { HelpTopic } from 'jde-spa';

//jde-opc's pages, registered by the site under HELP_TOPICS.  The markdown ships from assets/help (web/CLAUDE.md).
//'gateways' covers the whole browse tree as a prefix; the two apps/ patterns outrank jde-framework's 'apps' by literal count, and
//the historian's template and group pages, OEE's machine, line and reason pages, and the flows' pages, outrank 'gateways' by one
//more, and a line's floor (#481) outranks the lines by one more again.  'gateways/groups', 'gateways/oee', 'gateways/floor' and
//'gateways/flows' are split out of the gateways page.
export const opcHelpTopics:HelpTopic[] = [
	{ id: 'gateways', title: 'Gateways', summary: 'Gateways, server connections and node browsing', icon: 'hub', url: 'assets/jde-opc/help/gateways.md', routes: ['gateways', 'apps/gateways/:instance', 'apps/gateways/:instance/watch', 'apps/opcServers/:instance'] },
	{ id: 'gateways/groups', title: 'Historian groups', summary: 'Templates and groups - what the gateway historian keeps', icon: 'history', url: 'assets/jde-opc/help/groups.md', routes: ['apps/gateways/:instance/templates', 'apps/gateways/:instance/groups'] },
	{ id: 'gateways/oee', title: 'OEE', summary: 'Machines, lines, reasons and the dashboard - overall equipment effectiveness on the historian', icon: 'precision_manufacturing', url: 'assets/jde-opc/help/oee.md', routes: ['apps/gateways/:instance/machines', 'apps/gateways/:instance/lines', 'apps/gateways/:instance/reasons', 'apps/gateways/:instance/oee'] },
	{ id: 'gateways/floor', title: 'Floor map', summary: "A line's machines drawn where they stand, each outlined by its state, live", icon: 'map', url: 'assets/jde-opc/help/floor.md', routes: ['apps/gateways/:instance/lines/:slug/floor'] },
	{ id: 'gateways/flows', title: 'Flows', summary: 'Flows - nodes wired on a canvas, their trace and replay, and derived variables', icon: 'account_tree', url: 'assets/jde-opc/help/flows.md', routes: ['apps/gateways/:instance/flows'] }
];