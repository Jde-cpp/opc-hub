import { Flex, Style, TableSettings } from 'jde-framework';

//The Connections tab's list, the gateway route's data.tableSettings.
export const gatewayTableSettings:TableSettings = {
	empty: { title: "No server connections.", add: "Use Add to connect this gateway to an OPC server." },
	noun: "server connections",//the route title is the instance path - "No access to gateways/opchub.debug" named nothing to ask for
	//Read left to right:  what the connection is (name), how it is doing (status and the two counts that back it), then where it
	//points (url, and the certificate uri that has to match the server at that url), then the description.  The two uris are the
	//widest and least often read, so they sit after the state rather than pushing it off the side.
	//connectionStatus is grafted by the gateway (ql/OpcSessionsQLAwait.cpp), so it is an OBJECT with no `id` - the explicit selection is
	//required or query() asks for the framework's default `{id name}` and the server rejects the unknown column.  As plain text the
	//three states were indistinguishable, so a broken connection announced itself no louder than a healthy one;  as a chip only
	//Error is coloured to catch the eye - Idle is a resting state, not a warning.  The word stays on screen, so the meaning never
	//depends on the colour alone.  130px, not the ~110 the text needed:  a chip adds its own padding around the longest
	//label ("Connected"), and the cell's own 40px of padding on top of that, so a narrower column ellipsises it to "Connec...".
	//opcConnections shows as "Clients", not "Connections":  a row *is* a connection, so a column of that name under a tab of
	//that name read as a contradiction next to Sessions (16 sessions, 0 connections).  What it counts is the live UAClients
	//the gateway holds against the slug, which is what the gateway itself calls them.
	//`description` is listed last for readability only - View.columns() moves it there whatever position it is given.
	columns: [{name:"connectionStatus", displayName:"Status", selection:"name", style: new Style(130), chip:{Connected:"ok", Idle:"neutral", Error:"error"}}, "name", {name:"opcSessions", displayName:"Sessions", selection:"count", style: new Style({flex: new Flex(130), align:"right"})}, {name:"opcConnections", displayName:"Clients", selection:"count", style: new Style({flex: new Flex(130), align:"right"})}, {name:"url", displayName:"URL"}, {name:"certificateUri", displayName:"Certificate URI"}, "description" ],
	sort: "name"
}