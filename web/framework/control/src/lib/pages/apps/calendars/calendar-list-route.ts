import { Style } from '../../../model/ql/view';
import { ListRoute, TableSettings } from '../../../services/ql-list-resolver';

//The Calendars list's settings and routing (#396), shared by the tab (CalendarList) and the detail page's resolver
//(CalendarResolver), which adopts the collection and its settings for the row query and the breadcrumb.  A file of its own:  the
//resolver is in main, and must not bring the list or the page with it.
export const calendarTableSettings:TableSettings = {
	noun: 'calendars',
	empty: { title: 'No calendars yet.', detail: 'A calendar is when a plant is planned to run:  a time zone, the start of its production day, its shifts and its holidays, shared with every gateway.', add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 240 )}, {name: 'timeZone', displayName: 'Time zone', style: new Style( 220 )}, {name: 'dayStart', displayName: 'Day start'}, 'slug', 'description' ],
	sort: 'name'
};
export const calendarsPath = 'calendars';
export function calendarListRoute():ListRoute{
	return new ListRoute( { path: calendarsPath, title: 'Calendars', data: {summary: 'Shift calendars, shared with every gateway', collectionName: 'calendars', tableSettings: calendarTableSettings} } );
}