import { ListRoute, Style, TableSettings } from 'jde-framework';

export type OeeAdminKind = 'machines'|'lines'|'reasons';

//The OEE lists' settings and routing (#404, the lines' #409), as the historian's (hist-admin-list-route.ts):  shared by the tabs' list and the
//detail pages' resolver, in a file of its own so the resolver, which is in main, brings no page with it.
export const oeeMachineTableSettings:TableSettings = {
	noun: 'machines',
	empty: { title: 'No machines yet.', detail: "A machine is a historian group whose members are given roles - its run state, its counts, its stop reason - which the gateway turns into stops and OEE figures.", add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 240 )}, {name: 'histGroup', displayName: 'Group', selection: 'name', style: new Style( 200 )}, {name: 'countUnit', displayName: 'Count unit'}, 'slug', 'description' ],
	sort: 'name'
};
export const oeeLineTableSettings:TableSettings = {
	noun: 'lines',
	empty: { title: 'No lines yet.', detail: "A line is a named set of machines and of lines - a cell, a line, an area or the site - whose figures are its members' combined.", add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 240 )}, {name: 'level', style: new Style( 120 )}, 'slug', 'description' ],
	sort: 'name'
};
export const oeeReasonTableSettings:TableSettings = {
	noun: 'reasons',
	empty: { title: 'No reasons yet.', detail: "A reason is what a stop is coded with:  Unplanned, Planned or Excluded, with the code a machine's Reason signal reports for it.", add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 240 )}, {name: 'category', style: new Style( 160 )}, 'code', 'slug', 'description' ],
	sort: 'name'
};
export function oeeAdminListRoute( kind:OeeAdminKind ):ListRoute{
	return new ListRoute( kind=='machines'
		? { path: 'machines', title: 'Machines', data: {summary: 'Historian groups read as machines, for OEE', collectionName: 'oeeMachines', tableSettings: oeeMachineTableSettings} }
		: kind=='lines' ? { path: 'lines', title: 'Lines', data: {summary: 'Machines and lines whose figures are combined', collectionName: 'oeeLines', tableSettings: oeeLineTableSettings} }
		: { path: 'reasons', title: 'Reasons', data: {summary: 'What a stop is coded with', collectionName: 'oeeReasons', tableSettings: oeeReasonTableSettings} } );
}