import { ListRoute, Style, TableSettings } from 'jde-framework';

export type HistAdminKind = 'templates'|'groups';

//The lists' settings and routing, shared by the list (GatewayHistList) and the detail pages' resolver (HistAdminResolver), which
//adopts the collection and its settings for the row query and the breadcrumb.
export const histTemplateTableSettings:TableSettings = {
	noun: 'templates',
	empty: { title: 'No templates yet.', detail: 'A template is a node set, as browse paths from a root, that a group applies to one root on one server.', add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 300 )}, 'slug', 'description' ],
	sort: 'name'
};
//Read left to right:  the group, where it is - the connection, the template it applies, the root it applies it from - then the description.
//The connection's slug is for the root, which GatewayHistList shows by its browse path on the connection.
export const histGroupTableSettings:TableSettings = {
	noun: 'groups',
	empty: { title: 'No groups yet.', detail: 'A group is the nodes the gateway historizes on one connection:  a template applied to a root, or a node list of its own.', add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 240 )}, {name: 'serverConnection', displayName: 'Connection', selection: 'name slug', style: new Style( 200 )}, {name: 'histTemplate', displayName: 'Template', selection: 'name', style: new Style( 200 )}, {name: 'root', displayName: 'Root'}, 'slug', 'description' ],
	sort: 'name'
};
export function histAdminListRoute( kind:HistAdminKind ):ListRoute{
	return new ListRoute( kind=='templates'
		? { path: 'templates', title: 'Templates', data: {summary: 'Node sets the historian applies to a root', collectionName: 'histTemplates', tableSettings: histTemplateTableSettings} }
		: { path: 'groups', title: 'Groups', data: {summary: 'The nodes the gateway historizes', collectionName: 'histGroups', tableSettings: histGroupTableSettings} } );
}