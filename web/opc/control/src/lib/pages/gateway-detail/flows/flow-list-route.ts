import { ListRoute, Style, TableSettings } from 'jde-framework';

//The Flows tab's list (#455), as the historian's (hist-admin-list-route.ts):  shared by the tab and the flow page's resolver, in a
//file of its own so the resolver, which is in main, brings no page with it.  The list has no Add:  a flow is made by the tab's
//Create and Import, which need no page for a row with no graph.
export const flowTableSettings:TableSettings = {
	noun: 'flows',
	canAdd: false,
	empty: { title: 'No flows yet.', detail: 'A flow is a graph of nodes the gateway runs:  messages from its sources pass through its nodes to its sinks.  Create draws one;  Import reads one from a file.', icon: 'account_tree' },
	columns: [ {name: 'name', style: new Style( 240 )}, {name: 'enabled', style: new Style( 100 )}, {name: 'version', style: new Style( 100 )}, {name: 'trace', style: new Style( 100 )}, 'slug', 'description' ],
	sort: 'name'
};
export function flowListRoute():ListRoute{
	return new ListRoute( { path: 'flows', title: 'Flows', data: {summary: 'Graphs of nodes the gateway runs', collectionName: 'flows', tableSettings: flowTableSettings} } );
}