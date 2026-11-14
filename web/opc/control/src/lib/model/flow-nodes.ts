import { fromIsoDuration } from 'jde-framework';
import { FlowFault, FlowGraph, FlowNode } from './flow';
import { parseHistNodeText } from './hist-admin';

//The node types of a flow (#455, docs/flows/spec.md *Sources*, *Nodes*, *Sinks*), as the editor draws them and as a deploy holds
//them:  each type's ports, the fields of its `props`, which the side form shows, and the rules the gateway refuses a graph by,
//mirrored from the library (libs/flows Graph::Read, src/nodes) and the host (FlowNodes::Check), so the editor marks a fault before
//the deploy is sent.  The gateway's answer is the word that counts:  a Function body's compile, a node's rights, a group or a
//calendar that is not there, and a time zone, are its alone to refuse.

type Props = Record<string,unknown>;
export type FlowCategory = 'source'|'node'|'sink';
//text:  text that is not empty.  path:  a message property's path.  wait:  an ISO 8601 duration.  json:  any json value.
//code:  text, which may be empty.  list:  texts.  group:  an object of fields.  rules:  objects of fields, one at least.
//node:  a node of a connection, by its id.  scope:  ids of the graph's nodes.
export type FlowFieldKind = 'text'|'path'|'number'|'count'|'flag'|'choice'|'wait'|'json'|'code'|'list'|'group'|'rules'|'node'|'scope';
export interface FlowField{
	key:string;
	label:string;
	kind:FlowFieldKind;
	required?:string;//what is said of the key where it is absent
	dflt?:unknown;//what the node takes where the key is absent
	blank?:string;//what a choice left unset means, where that is none of its options
	hint?:string;
	options?:readonly string[];//a choice's, and what a list may hold
	labels?:Record<string,string>;//the options as the form names them
	zero?:boolean;//a wait of zero is one
	orNumber?:boolean;//text, or a row's id
	orObject?:boolean;//text, or an object
	item?:'node'|'time';//what a list's texts are
	fields?:( value:Props )=>FlowField[];//a group's, and a rule's by what it holds
}
export interface FlowNodeType{
	type:string;
	category:FlowCategory;
	icon:string;
	about:string;
	input:boolean;//whether a wire may end at it
	outputs:( props:Props )=>number;
	fields:( props:Props )=>FlowField[];
	defaults?:()=>Props;//a new node's props
	check?:( props:Props, node:FlowNode, graph:FlowGraph )=>void;//the rules past a field's own:  throws what is wrong
}

const absent = ( v:unknown )=>v===undefined || v===null;
const noRules = '`rules` must be a list of rules, one at least.';
const isObject = ( v:unknown ):v is Props=>!!v && typeof v=='object' && !Array.isArray( v );
const fail = ( message:string ):never=>{ throw new Error( message ); };

//An ISO 8601 duration in seconds, as Chrono::TryToDuration reads one;  none for text that is not one.
export function flowWaitSeconds( text:unknown ):number|undefined{
	if( typeof text!='string' || text.length<3 )//P1D at the least:  a bare P has no component
		return undefined;
	try{
		return fromIsoDuration( text );
	}
	catch{
		return undefined;
	}
}
//What is wrong with a property's path, as Flows::Path refuses one:  names joined by '.', an index in brackets after a name.
export function flowPathFault( text:string ):string|undefined{
	const refuse = ( why:string )=>`'${text}' is not a property's path:  ${why}.`;
	let steps = 0;
	for( let i=0; i<text.length; ){
		if( text[i]=='[' ){
			const end = text.indexOf( ']', i );
			if( !steps || end<0 || !/^\d+$/.test( text.substring( i+1, end ) ) )
				return refuse( 'an index is a number in brackets after a name' );
			i = end+1;
		}
		else{
			if( steps && text[i++]!='.' )
				return refuse( "a name follows a '.'" );
			const next = text.substring( i ).search( /[.[]/ );
			const end = next<0 ? text.length : i+next;
			if( end==i )
				return refuse( 'a name is missing' );
			i = end;
		}
		++steps;
	}
	return steps ? undefined : refuse( 'it is empty' );
}
const pathRoot = ( text:string )=>text.split( /[.[]/ )[0];
//a node of a connection, as ParseHistNode reads one:  ns=2;s=Pump1.Rpm, its json text, or the json itself
function isNodeId( v:unknown ):boolean{
	if( typeof v=='string' )
		return !!parseHistNodeText( v );
	return isObject( v ) && ['i', 's', 'g', 'b'].some( k=>v[k]!==undefined );
}
const isTimeOfDay = ( v:unknown )=>{
	const parts = typeof v=='string' ? v.split( ':' ) : [];
	const [h, m, s] = [parts[0], parts[1], parts[2] ?? '0'].map( p=>p!==undefined && /^\d+$/.test( p ) ? Number( p ) : NaN );
	return parts.length>=2 && parts.length<=3 && h<=23 && m<=59 && s<=59;
};

//What is wrong with a field's value in `o`, as the library's Prop:: readers refuse one;  nothing for one that reads.
function checkField( f:FlowField, o:Props, graph:FlowGraph ):void{
	const v = o[f.key], key = f.key;
	if( f.kind=='json' ){//a literal:  null is one, so the key is there or it is not
		if( f.required && v===undefined )
			fail( f.required );
		return;
	}
	if( absent( v ) ){
		if( f.required )
			fail( f.required );
		return;
	}
	switch( f.kind ){
	case 'text':
	case 'path':
		if( f.orNumber && typeof v=='number' )
			return;
		if( f.orObject && isObject( v ) )
			return;
		if( typeof v!='string' || !v.length )
			fail( f.required ?? `\`${key}\` must be text.` );
		if( f.kind=='path' ){
			const fault = flowPathFault( <string>v );
			if( fault )
				fail( fault );
		}
		break;
	case 'number':
		if( typeof v!='number' || !Number.isFinite( v ) )
			fail( `\`${key}\` must be a number.` );
		break;
	case 'count':
		if( typeof v!='number' || !Number.isInteger( v ) || v<=0 )
			fail( `\`${key}\` must be a whole number above zero.` );
		break;
	case 'flag':
		if( typeof v!='boolean' )
			fail( `\`${key}\` must be true or false.` );
		break;
	case 'choice':
		if( typeof v!='string' || !f.options!.includes( v ) )
			fail( `\`${key}\` must be one of ${f.options!.join( ', ' )}.` );
		break;
	case 'wait':{
		const seconds = flowWaitSeconds( v );
		if( seconds===undefined || seconds<0 || (!f.zero && seconds==0) )
			fail( `\`${key}\` must be an ISO 8601 duration ${f.zero ? 'of zero or more' : 'above zero'}, as PT30S is.` );
		break;
	}
	case 'code':
		if( typeof v!='string' )
			fail( f.required ?? `\`${key}\` must be text.` );
		break;
	case 'list':
		if( !Array.isArray( v ) )
			fail( `\`${key}\` must be a list${f.options ? ` of ${f.options.join( ', ' )}` : ''}.` );
		for( const item of <unknown[]>v ){
			if( f.options && !f.options.includes( <string>item ) )
				fail( `\`${key}\` holds ${JSON.stringify( item )}, which is not one of ${f.options.join( ', ' )}.` );
			if( f.item=='node' && !isNodeId( item ) )
				fail( `${JSON.stringify( item )} is not a node id.` );
			if( f.item=='time' && !isTimeOfDay( item ) )
				fail( `\`${key}\` holds ${JSON.stringify( item )}, which is not a time of day, as 06:00 or 14:30:15.` );
		}
		break;
	case 'group':
		if( !isObject( v ) )
			fail( `\`${key}\` must be an object of ${f.fields!( {} ).map( x=>x.key ).join( ', ' )}.` );
		checkFields( f.fields!( <Props>v ), <Props>v, graph );
		break;
	case 'rules':{
		const rules = Array.isArray( v ) ? v : [];
		if( !rules.length || !rules.every( isObject ) )
			fail( noRules );
		rules.forEach( ( rule:Props, i:number )=>{
			try{
				checkFields( f.fields!( rule ), rule, graph );
			}
			catch( e ){
				fail( `Rule ${i+1}:  ${(<Error>e).message}` );
			}
		} );
		break;
	}
	case 'node':
		if( !isNodeId( v ) )
			fail( `${JSON.stringify( v )} is not a node id, as ns=2;s=Pump1.Rpm is.` );
		break;
	case 'scope':
		if( !Array.isArray( v ) )
			fail( `\`${key}\` must be a list of node ids.` );
		for( const id of <unknown[]>v ){
			if( typeof id!='string' || !graph.nodes.some( n=>n.id==id ) )
				fail( `\`${key}\` names ${JSON.stringify( id )}, which is not a node of the graph.` );
		}
		break;
	}
}
function checkFields( fields:FlowField[], o:Props, graph:FlowGraph ):void{
	for( const f of fields )
		checkField( f, o, graph );
}

const property:FlowField = { key: 'property', label: 'Property', kind: 'path', dflt: 'payload', hint: "The message's property the node works on." };
const scopes = ['msg', 'flow', 'global'] as const;
const scopeLabels = { msg: 'msg.', flow: 'flow.', global: 'global.' };
//a property of the message or a key of a context, as a change rule names what it writes (Change.cpp Target)
const target = ( rule:Props, key:string, typeKey:string, label:string ):FlowField[]=>[
	{ key: typeKey, label: `${label} in`, kind: 'choice', options: scopes, labels: scopeLabels, dflt: 'msg' },
	{ key, label, kind: absent( rule[typeKey] ) || rule[typeKey]=='msg' ? 'path' : 'text', required: `\`${key}\` must be text.` }
];
//a literal, or what a property or a context key holds (Operand.cpp)
const operand = ( rule:Props, key:string, typeKey:string, label:string ):FlowField[]=>[
	{ key: typeKey, label: `${label} is`, kind: 'choice', options: scopes, labels: scopeLabels, blank: 'a literal' },
	absent( rule[typeKey] )
		? { key, label, kind: 'json', required: `the rule has no \`${key}\`.`, hint: 'json:  10, "text", true, null' }
		: { key, label, kind: rule[typeKey]=='msg' ? 'path' : 'text', required: `\`${key}\` must be text.` }
];
export const flowSwitchOps = ['eq', 'neq', 'lt', 'lte', 'gt', 'gte', 'btwn', 'cont', 'regex', 'true', 'false', 'null', 'nnull', 'empty', 'nempty', 'istype', 'else'] as const;
const switchOpLabels = { eq: '==', neq: '!=', lt: '<', lte: '<=', gt: '>', gte: '>=', btwn: 'is between', cont: 'contains', regex: 'matches', true: 'is true', false: 'is false', null: 'is null', nnull: 'is not null', empty: 'is empty', nempty: 'is not empty', istype: 'is of type', else: 'otherwise' };
const switchRule = ( rule:Props ):FlowField[]=>{
	const op = flowSwitchOps.indexOf( <any>rule['t'] );
	const y:FlowField[] = [{ key: 't', label: 'When it', kind: 'choice', options: flowSwitchOps, labels: switchOpLabels, required: `\`t\` must be one of ${flowSwitchOps.join( ', ' )}.` }];
	if( op>=0 && op<=7 )
		y.push( ...operand( rule, 'v', 'vt', 'Value' ) );
	if( rule['t']=='btwn' )
		y.push( ...operand( rule, 'v2', 'v2t', 'And' ) );
	else if( rule['t']=='istype' )
		y.push( { key: 'v', label: 'Type', kind: 'choice', options: ['string', 'number', 'boolean', 'array', 'object', 'null'], required: '`v` must be one of string, number, boolean, array, object, null.' } );
	else if( rule['t']=='regex' )
		y.push( { key: 'v', label: 'Regular expression', kind: 'text', required: '`v` must be text.' }, { key: 'case', label: 'Ignore case', kind: 'flag', dflt: false } );
	return y;
};
const changeOps = ['set', 'delete', 'move'] as const;
const changeRule = ( rule:Props ):FlowField[]=>{
	const y:FlowField[] = [{ key: 't', label: 'Do', kind: 'choice', options: changeOps, required: '`t` must be one of set, delete, move.' }, ...target( rule, 'p', 'pt', 'Property' )];
	if( rule['t']=='set' )
		y.push( ...operand( rule, 'to', 'tot', 'To' ) );
	else if( rule['t']=='move' )
		y.push( ...target( rule, 'to', 'tot', 'To' ) );
	return y;
};
const connection:FlowField = { key: 'connection', label: 'Connection', kind: 'text', required: "`connection` is required, a connection's slug.", hint: "A connection's slug." };
const node:FlowField = { key: 'node', label: 'Node', kind: 'node', required: "`node` is required, the node's id.", hint: 'ns=2;s=Pump1.Rpm, or {"nsu":"urn:…","s":"Pump1.Rpm"}' };
const levels = ['Trace', 'Debug', 'Information', 'Warning', 'Error'] as const;
const scope = ( required?:string ):FlowField=>({ key: 'scope', label: 'From', kind: 'scope', required, hint: required ? 'The nodes it takes from.' : 'The nodes it takes from;  none is every node.' });
const one = ()=>1, none = ()=>0;
const noFields = ():FlowField[]=>[];
//a built-in type a variable is written as (FlowServer builtIn):  Boolean to LocalizedText, by its name in any case
const builtInTypes = ['boolean', 'sbyte', 'byte', 'int16', 'uint16', 'int32', 'uint32', 'int64', 'uint64', 'float', 'double', 'string', 'datetime', 'guid', 'bytestring', 'xmlelement', 'nodeid', 'expandednodeid', 'statuscode', 'qualifiedname', 'localizedtext'];
const httpMethods = ['GET', 'HEAD', 'POST', 'PUT', 'DELETE', 'PATCH', 'OPTIONS', 'CONNECT', 'TRACE'];

//the template's tags, as Mustache reads them:  variables alone
function checkMustache( template:string ):void{
	for( let text = template; text.length; ){
		const open = text.indexOf( '{{' );
		if( open<0 )
			break;
		const raw = text.startsWith( '{{{', open ), width = raw ? 3 : 2;
		const close = text.indexOf( raw ? '}}}' : '}}', open );
		if( close<0 )
			fail( 'the template has a tag that does not close.' );
		const name = text.substring( open+width, close ).trim();
		text = text.substring( close+width );
		if( name && '#^/>='.includes( name[0] ) )
			fail( `'${name}':  the template's mustache is its variables alone, with no sections, partials or delimiters.` );
	}
}
function checkUrl( url:string ):void{
	const parts = /^([A-Za-z][A-Za-z0-9+.-]*):\/\/([^/?#]*)/.exec( url );
	const scheme = parts?.[1].toLowerCase();
	if( scheme!='http' && scheme!='https' )
		fail( `'${url}' is not an http or https url.` );
	const authority = parts![2];
	if( authority.includes( '@' ) )
		fail( `'${url}' carries a user, which a flow's request does not send.` );
	const port = /:(\d*)$/.exec( authority.replace( /^\[[^\]]*\]/, '' ) );
	if( !authority.replace( /:\d*$/, '' ) )
		fail( `'${url}' names no host.` );
	if( port && (!port[1] || Number( port[1] )>65535) )
		fail( `'${url}' has a port that is not one.` );
}

export const flowNodeTypes:FlowNodeType[] = [
	{
		type: 'opc in', category: 'source', icon: 'sensors', about: "A node's data changes, each a message.", input: false, outputs: one,
		fields: ()=>[ connection, node,
			{ key: 'samplingInterval', label: 'Sampling interval, ms', kind: 'number', hint: "0 is the server's fastest;  blank the gateway's." },
			{ key: 'queueSize', label: 'Queue size', kind: 'number' },
			{ key: 'discardOldest', label: 'A full queue drops its oldest', kind: 'flag', dflt: true },
			{ key: 'filter', label: 'Data change filter', kind: 'group', fields: ()=>[
				{ key: 'trigger', label: 'Trigger', kind: 'choice', options: ['status', 'statusValue', 'statusValueTimestamp'], dflt: 'statusValue' },
				{ key: 'deadbandType', label: 'Deadband', kind: 'choice', options: ['none', 'absolute', 'percent'], dflt: 'none' },
				{ key: 'deadbandValue', label: 'Deadband value', kind: 'number', dflt: 0 }
			] }
		],
		check: props=>{
			const queue = props['queueSize'];
			if( !absent( queue ) && (!Number.isInteger( queue ) || <number>queue<0 || <number>queue>0xFFFFFFFF) )
				fail( '`queueSize` must be a whole number.' );
			if( isObject( props['filter'] ) && <number>props['filter']['deadbandValue']<0 )
				fail( '`deadbandValue` must not be negative.' );
		}
	},
	{
		type: 'group in', category: 'source', icon: 'history', about: "A historian group's records from a time on, then as they are stored.", input: false, outputs: one,
		fields: ()=>[
			{ key: 'group', label: 'Group', kind: 'text', orNumber: true, required: "`group` is required, a historian group's slug or id.", hint: "A historian group's slug." },
			{ key: 'members', label: 'Members', kind: 'list', item: 'node', hint: 'Node ids, one a line;  none is every member.' },
			{ key: 'from', label: 'From', kind: 'text', dflt: 'resume', hint: 'resume, where it left off;  now;  or a time, 2026-03-09T06:00:00Z.' },
			{ key: 'lateness', label: 'Lateness', kind: 'wait', zero: true, hint: "How far before its mark a resume reads;  blank the group's heartbeat gap." },
			{ key: 'modifications', label: 'A modification is a message of its own', kind: 'flag', dflt: false }
		],
		check: props=>{
			const from = props['from'];
			if( typeof from=='string' && from!='resume' && from!='now' && (isNaN( new Date( from ).getTime() ) || !/^\d{4}-\d{2}-\d{2}/.test( from )) )
				fail( `\`from\` must be resume, now or a time, as 2026-03-09T06:00:00Z, not '${from}'.` );
		}
	},
	{
		type: 'timer', category: 'source', icon: 'timer', about: 'A tick every so often, or at times of day.', input: false, outputs: one,
		fields: ()=>[
			{ key: 'every', label: 'Every', kind: 'wait', hint: 'An ISO 8601 duration, PT30S, aligned to the hour.  Or At, below.' },
			{ key: 'at', label: 'At', kind: 'group', fields: ()=>[
				{ key: 'times', label: 'Times', kind: 'list', item: 'time', hint: 'Local times of day, one a line:  06:00, 14:30:15.' },
				{ key: 'days', label: 'Days', kind: 'list', options: ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'], hint: 'None is every day.' },
				{ key: 'zone', label: 'Time zone', kind: 'text', hint: "An IANA zone, Europe/Berlin;  blank the gateway's." }
			] }
		],
		defaults: ()=>({ every: 'PT1M' }),
		check: props=>{
			const every = flowWaitSeconds( props['every'] ), at = props['at'];
			if( every!==undefined && !absent( at ) )
				fail( 'A timer takes `every` or `at`, not both.' );
			if( every===undefined && absent( at ) )
				fail( 'A timer takes `every`, an ISO 8601 duration, or `at`, local times.' );
			if( every!==undefined && every<.001 )
				fail( '`every` must be a millisecond at least.' );
			if( isObject( at ) ){
				if( !Array.isArray( at['times'] ) || !at['times'].length )
					fail( '`at.times` must be a list of local times, as 06:00.' );
				if( Array.isArray( at['days'] ) && !at['days'].length )
					fail( '`at.days` names no day.' );
			}
		}
	},
	{
		type: 'calendar', category: 'source', icon: 'calendar_month', about: "A shift calendar's boundaries, each a message.", input: false, outputs: one,
		fields: ()=>[
			{ key: 'calendar', label: 'Calendar', kind: 'text', orNumber: true, required: "`calendar` is required, a calendar's slug or id.", hint: "A calendar's slug." },
			{ key: 'boundaries', label: 'Boundaries', kind: 'list', options: ['shiftStart', 'shiftEnd', 'breakStart', 'breakEnd'], hint: 'None is all four.' }
		]
	},
	{
		type: 'connection', category: 'source', icon: 'lan', about: "A connection's loss and return.", input: false, outputs: one,
		fields: ()=>[ connection ]
	},
	{
		type: 'inject', category: 'source', icon: 'input', about: 'A message sent by hand, from its button.', input: false, outputs: one,
		fields: ()=>[ { key: 'message', label: 'Message', kind: 'json', hint: 'The message it sends, a json object:  { "payload": 1 }.' } ],
		defaults: ()=>({ message: {payload: 1} }),
		check: props=>{
			if( !absent( props['message'] ) && !isObject( props['message'] ) )
				fail( '`message` must be an object, the message the node sends.' );
		}
	},
	{
		type: 'link in', category: 'source', icon: 'login', about: "What a link out of its name sends, this flow's or another's.", input: false, outputs: one,
		fields: ()=>[ { key: 'name', label: 'Link', kind: 'text', required: '`name` must be text.', hint: 'The name its link out nodes send to.' } ]
	},
	{
		type: 'switch', category: 'node', icon: 'call_split', about: "Routes a message by rules over a property, an output a rule.", input: true,
		outputs: props=>Array.isArray( props['rules'] ) ? props['rules'].length : 0,
		fields: ()=>[ property, { key: 'checkall', label: 'Send to every rule that matches', kind: 'flag', dflt: true }, { key: 'rules', label: 'Rules', kind: 'rules', required: noRules, fields: switchRule } ],
		defaults: ()=>({ rules: [{t: 'eq', v: 0}, {t: 'else'}] }),
		check: props=>{
			for( const [i, rule] of (<Props[]>props['rules']).entries() ){
				if( rule['t']!='regex' )
					continue;
				try{
					new RegExp( <string>rule['v'] );
				}
				catch( e ){
					fail( `Rule ${i+1}:  '${rule['v']}' is not a regular expression:  ${(<Error>e).message}` );
				}
			}
		}
	},
	{
		type: 'change', category: 'node', icon: 'edit', about: 'Sets, deletes or moves properties and context keys.', input: true, outputs: one,
		fields: ()=>[ { key: 'rules', label: 'Rules', kind: 'rules', required: noRules, fields: changeRule } ],
		defaults: ()=>({ rules: [{t: 'set', p: 'payload', to: 0}] }),
		check: props=>{
			for( const [i, rule] of (<Props[]>props['rules']).entries() ){
				for( const [key, typeKey] of [['p', 'pt'], ...( rule['t']=='move' ? [['to', 'tot']] : [] )] ){
					const root = absent( rule[typeKey] ) || rule[typeKey]=='msg' ? pathRoot( <string>rule[key] ) : '';
					if( root=='_id' || root=='_parent' )
						fail( `Rule ${i+1}:  \`${rule[key]}\` is the engine's:  no rule changes a message's \`_id\` or \`_parent\`.` );
				}
			}
		}
	},
	{
		type: 'filter', category: 'node', icon: 'filter_alt', about: 'Passes a message whose property changed, or changed by more than a band.', input: true, outputs: one,
		fields: ()=>[ property,
			{ key: 'band', label: 'Band', kind: 'number', hint: 'How far it must change;  blank is any change.' },
			{ key: 'percent', label: 'The band is a percent of the last value', kind: 'flag', dflt: false },
			{ key: 'perTopic', label: 'Each topic apart', kind: 'flag', dflt: true }
		],
		check: props=>{
			if( <number>props['band']<0 )
				fail( '`band` must not be below zero.' );
			if( props['percent']===true && absent( props['band'] ) )
				fail( '`percent` is of a `band`.' );
		}
	},
	{
		type: 'range', category: 'node', icon: 'straighten', about: 'Scales a number from one range to another.', input: true, outputs: one,
		fields: ()=>[ property,
			...['minIn', 'maxIn', 'minOut', 'maxOut'].map( ( key ):FlowField=>({ key, label: `${key.endsWith( 'In' ) ? 'Input' : 'Output'} ${key.startsWith( 'min' ) ? 'from' : 'to'}`, kind: 'number', required: `\`${key}\` must be a number.` }) ),
			{ key: 'action', label: 'Past the range', kind: 'choice', options: ['scale', 'clamp', 'wrap'], dflt: 'scale' },
			{ key: 'round', label: 'Round to a whole number', kind: 'flag', dflt: false }
		],
		defaults: ()=>({ minIn: 0, maxIn: 100, minOut: 0, maxOut: 1 }),
		check: props=>{
			if( props['minIn']===props['maxIn'] )
				fail( '`minIn` and `maxIn` are the same:  the input range is empty.' );
		}
	},
	{
		type: 'delay', category: 'node', icon: 'hourglass_empty', about: 'Holds each message for a time, or limits their rate.', input: true, outputs: one,
		fields: props=>[
			{ key: 'mode', label: 'Mode', kind: 'choice', options: ['delay', 'rate'], labels: {delay: 'Hold each message', rate: 'Limit the rate'}, dflt: 'delay' },
			...( props['mode']=='rate'
				? <FlowField[]>[ { key: 'count', label: 'Messages', kind: 'count', dflt: 1 }, { key: 'per', label: 'Per', kind: 'wait', required: '`per` must be an ISO 8601 duration above zero, as PT30S is.' } ]
				: <FlowField[]>[ { key: 'duration', label: 'For', kind: 'wait', required: '`duration` must be an ISO 8601 duration above zero, as PT30S is.' } ] ),
			{ key: 'drop', label: 'Drop what would wait', kind: 'flag', dflt: false }
		],
		defaults: ()=>({ duration: 'PT1S' })
	},
	{
		type: 'trigger', category: 'node', icon: 'bolt', about: 'Sends a value at a message and another after a wait, unless reset:  the watchdog.', input: true, outputs: one,
		fields: ()=>[
			{ key: 'wait', label: 'Wait', kind: 'wait', required: '`wait` must be an ISO 8601 duration above zero, as PT30S is.' },
			{ key: 'first', label: 'Send first', kind: 'json', hint: 'A json value;  blank sends nothing at the message.' },
			{ key: 'second', label: 'Then send', kind: 'json', hint: 'A json value;  blank sends nothing after the wait.' },
			{ key: 'reset', label: 'Reset by', kind: 'json', hint: 'A payload that takes the wait back.' },
			{ key: 'extend', label: 'Each message starts the wait again', kind: 'flag', dflt: false },
			{ key: 'perTopic', label: 'Each topic apart', kind: 'flag', dflt: false }
		],
		defaults: ()=>({ wait: 'PT30S', second: 'no value for 30 s', extend: true }),
		check: props=>{
			if( absent( props['first'] ) && absent( props['second'] ) )
				fail( 'a trigger sends a `first`, a `second` or both.' );
		}
	},
	{
		type: 'window', category: 'node', icon: 'view_week', about: "A window's count, sum, min, max, mean, first, last and delta, at its end.", input: true, outputs: one,
		fields: ()=>[ property,
			{ key: 'kind', label: 'Kind', kind: 'choice', options: ['tumbling', 'sliding'], dflt: 'tumbling' },
			{ key: 'duration', label: 'Duration', kind: 'wait', hint: 'By time, PT1M.  Or by count, below.' },
			{ key: 'count', label: 'Count', kind: 'count' },
			{ key: 'perTopic', label: 'Each topic apart', kind: 'flag', dflt: true }
		],
		defaults: ()=>({ duration: 'PT1M' }),
		check: props=>{
			if( absent( props['duration'] )==absent( props['count'] ) )
				fail( 'a window is by `duration` or by `count`, one of the two.' );
		}
	},
	{ type: 'split', category: 'node', icon: 'alt_route', about: 'An array or an object as a sequence of messages.', input: true, outputs: one, fields: noFields },
	{
		type: 'join', category: 'node', icon: 'merge', about: 'A sequence back into an array or an object.', input: true, outputs: one,
		fields: ()=>[
			{ key: 'mode', label: 'Into', kind: 'choice', options: ['auto', 'array', 'object'], labels: {auto: "What a split's parts say"}, dflt: 'auto' },
			{ key: 'key', label: 'Key', kind: 'path', dflt: 'topic', hint: "The property an object's parts are named by." },
			{ key: 'count', label: 'Count', kind: 'count', hint: 'Send after this many.' },
			{ key: 'timeout', label: 'Timeout', kind: 'wait', hint: 'Send what came this long after the first.' }
		],
		check: props=>{
			if( !absent( props['mode'] ) && props['mode']!='auto' && absent( props['count'] ) && absent( props['timeout'] ) )
				fail( 'a join into an array or an object needs a `count` or a `timeout`.' );
		}
	},
	{
		type: 'template', category: 'node', icon: 'text_snippet', about: "Text from the message's properties, {{payload}}.", input: true, outputs: one,
		fields: ()=>[ property,
			{ key: 'template', label: 'Template', kind: 'code', required: '`template` must be text.', hint: 'Mustache variables alone:  {{payload}}, {{flow.name}}, {{{raw}}}.' },
			{ key: 'output', label: 'As', kind: 'choice', options: ['text', 'json'], dflt: 'text' }
		],
		defaults: ()=>({ template: '{{payload}}' }),
		check: props=>checkMustache( <string>props['template'] )
	},
	{
		type: 'function', category: 'node', icon: 'code', about: "JavaScript on the message, Node-RED's Function API.", input: true,
		outputs: props=>{ const n = props['outputs']; return absent( n ) ? 1 : Number.isInteger( n ) && <number>n>=0 && <number>n<=100 ? <number>n : 1; },
		fields: ()=>[
			{ key: 'code', label: 'On message', kind: 'code', required: '`code` must be text, the body.', hint: 'The body of an async function of msg:  return msg, an array an output, or null.' },
			{ key: 'outputs', label: 'Outputs', kind: 'number', dflt: 1 },
			{ key: 'onStart', label: 'On start', kind: 'code', hint: 'Run when the flow starts.' },
			{ key: 'onStop', label: 'On stop', kind: 'code', hint: 'Run when the flow stops.' }
		],
		defaults: ()=>({ code: 'return msg;' }),
		check: props=>{
			const n = props['outputs'];
			if( !absent( n ) && (!Number.isInteger( n ) || <number>n<0 || <number>n>100) )
				fail( '`outputs` must be a whole number, 100 at most.' );
		}
	},
	{
		type: 'debug', category: 'node', icon: 'bug_report', about: 'Marks a point the debug panel shows;  a level sends it to the log too.', input: true, outputs: none,
		fields: ()=>[ { key: 'active', label: 'Active', kind: 'flag', dflt: true }, { key: 'level', label: 'Log at', kind: 'choice', options: levels, blank: 'not logged' } ]
	},
	{
		type: 'catch', category: 'node', icon: 'report', about: "A node's error, with the message.", input: false, outputs: one,
		fields: ()=>[ { key: 'uncaught', label: 'Only what no other catch takes', kind: 'flag', dflt: false }, scope() ]
	},
	{ type: 'status', category: 'node', icon: 'info', about: "A node's status, each change.", input: false, outputs: one, fields: ()=>[ scope() ] },
	{
		type: 'complete', category: 'node', icon: 'task_alt', about: 'Each message a chosen node finishes.', input: false, outputs: one,
		fields: ()=>[ scope( '`scope` must name the nodes a complete node takes from.' ) ],
		check: props=>{
			if( !(<unknown[]>props['scope']).length )
				fail( '`scope` must name the nodes a complete node takes from.' );
		}
	},
	{
		type: 'link out', category: 'node', icon: 'logout', about: "Sends to the link in of its name, this flow's or another's.", input: true, outputs: none,
		fields: ()=>[ { key: 'name', label: 'Link', kind: 'text', required: '`name` must be text.', hint: 'The link in it sends to.' } ]
	},
	{ type: 'comment', category: 'node', icon: 'sticky_note_2', about: 'Text on the canvas:  its name.', input: false, outputs: none, fields: noFields },
	{
		type: 'opc out', category: 'sink', icon: 'upload', about: 'Writes a node, and waits for its server.', input: true, outputs: one,
		fields: ()=>[ connection, node, {...property, hint: "The message's property written."} ]
	},
	{
		type: 'variable', category: 'sink', icon: 'data_object', about: 'A derived variable on the flow server, a node like any other.', input: true, outputs: none,
		fields: ()=>[
			{ key: 'name', label: 'Variable', kind: 'text', required: '`name` must be text.', hint: "Its name under the flow's folder." },
			{ key: 'dataType', label: 'Data type', kind: 'text', required: '`dataType` must be text.', hint: 'A built-in type, Double, or an array of one, Double[].' },
			{ key: 'description', label: 'Description', kind: 'text' },
			{ key: 'engineeringUnits', label: 'Engineering units', kind: 'text', orObject: true, hint: "The unit's text, or Part 8's object." },
			{ key: 'euRange', label: 'Range', kind: 'group', fields: ()=>[
				{ key: 'low', label: 'Low', kind: 'number', required: '`low` must be a number.' },
				{ key: 'high', label: 'High', kind: 'number', required: '`high` must be a number.' }
			] }
		],
		defaults: ()=>({ dataType: 'Double' }),
		check: ( props, self, graph )=>{
			const name = <string>props['name'], type = (<string>props['dataType']).replace( /\[\]$/, '' );
			if( name.includes( '/' ) )
				fail( `\`name\` '${name}' holds a '/', which a node's path is split by.` );
			if( name=='_Flow' )
				fail( `\`name\` '${name}' is the folder's own object.` );
			if( /[\u0000-\u001f]/.test( name ) )
				fail( '`name` holds a control character.' );
			if( !builtInTypes.includes( type.toLowerCase() ) )
				fail( `\`dataType\` '${type}' is no built-in type a flow writes, as Double, Int32, Boolean, String and DateTime are, or an array of one, \`Double[]\`.` );
			const range = props['euRange'];
			if( isObject( range ) && !(Number( range['low'] )<Number( range['high'] )) )
				fail( "`euRange`'s low must be under its high." );
			if( graph.nodes.some( n=>n!==self && n.type=='variable' && n.props['name']===name ) )
				fail( `Two variable nodes are named '${name}'.` );
		}
	},
	{
		type: 'log', category: 'sink', icon: 'notes', about: "A line in the gateway's log.", input: true, outputs: none,
		fields: ()=>[
			{ key: 'template', label: 'Line', kind: 'text', dflt: '{{payload}}', hint: 'Mustache variables alone.' },
			{ key: 'level', label: 'Level', kind: 'choice', options: levels, dflt: 'Information' }
		],
		check: props=>{
			if( typeof props['template']=='string' )
				checkMustache( props['template'] );
		}
	},
	{
		type: 'http request', category: 'sink', icon: 'public', about: 'A request to an allowed host;  its response goes on.', input: true, outputs: one,
		fields: ()=>[
			{ key: 'url', label: 'Url', kind: 'text', hint: "Blank takes the message's `url`.  Its host must be one the gateway allows." },
			{ key: 'method', label: 'Method', kind: 'text', hint: "Blank takes the message's `method`, else GET." },
			{ key: 'contentType', label: 'Content type', kind: 'text', hint: "Blank is text/plain for text, else application/json." },
			{ key: 'output', label: 'Response as', kind: 'choice', options: ['text', 'json'], dflt: 'text' }
		],
		check: props=>{
			if( typeof props['url']=='string' )
				checkUrl( props['url'] );
			const method = props['method'];
			if( typeof method=='string' && !httpMethods.includes( method.toUpperCase() ) )
				fail( `'${method}' is not an http method.` );
		}
	}
];
const byType = new Map( flowNodeTypes.map( t=>[t.type, t] ) );
export function flowNodeType( type:string ):FlowNodeType|undefined{ return byType.get( type ); }
export const flowCategories:{ category:FlowCategory; label:string }[] = [ {category: 'source', label: 'Sources'}, {category: 'node', label: 'Nodes'}, {category: 'sink', label: 'Sinks'} ];
//a node's output ports:  a switch's a rule, a Function's its `outputs`;  none for a type that is not one
export function flowOutputs( node:Pick<FlowNode,'type'|'props'> ):number{ return flowNodeType( node.type )?.outputs( node.props ) ?? 0; }

//What is wrong with a node's own props, as the side form says it:  nothing for a node that reads.
export function flowNodeFault( node:FlowNode, graph:FlowGraph ):string|undefined{
	const type = flowNodeType( node.type );
	if( !type )
		return `\`type\` "${node.type}" is not a node type.`;
	try{
		checkFields( type.fields( node.props ), node.props, graph );
		type.check?.( node.props, node, graph );
	}
	catch( e ){
		return (<Error>e).message;
	}
	return undefined;
}
//each node's fault, its props' or its wires', by its id:  what the canvas warns of
export function flowNodeFaults( graph:FlowGraph ):Map<string,string>{
	const y = new Map<string,string>();
	for( const node of graph.nodes ){
		const fault = flowNodeFault( node, graph ) ?? wireFault( node, graph );
		if( fault )
			y.set( node.id, fault );
	}
	return y;
}
//The first fault of a graph, in node order, as Graph::Read finds it and a deploy refuses by:  an id missing or used twice, a type
//that is not one, a prop that is not of its type, two `link in` of a name, and a wire from a port its node lacks, to no node of
//the graph or to one that takes no input.  A `link out` whose name no `link in` here holds is no fault:  another flow's may.
export function flowGraphFault( graph:FlowGraph ):FlowFault|undefined{
	const seen = new Set<string>();
	for( const [i, node] of graph.nodes.entries() ){
		if( !node.id )
			return { message: `Node ${i+1} of the graph has no \`id\`.` };
		if( seen.has( node.id ) )
			return { node: node.id, message: 'Two nodes have this id.' };
		seen.add( node.id );
		if( !flowNodeType( node.type ) )
			return { node: node.id, message: `\`type\` ${node.type ? JSON.stringify( node.type ) : 'is missing, and so'} is not a node type.` };
	}
	for( const node of graph.nodes ){
		const fault = flowNodeFault( node, graph ) ?? wireFault( node, graph );
		if( fault )
			return { node: node.id, message: fault };
	}
	return undefined;
}
function wireFault( node:FlowNode, graph:FlowGraph ):string|undefined{
	const name = node.props['name'];
	if( node.type=='link in' && graph.nodes.filter( n=>n.type=='link in' && n.props['name']===name ).length>1 )
		return `Two \`link in\` nodes are named '${name}'.`;
	const outputs = flowOutputs( node );
	for( const [port, targets] of node.wires.entries() ){
		if( port>=outputs && targets.length )
			return `Output ${port+1} is wired, and a '${node.type}' node has ${outputs}.`;
		for( const id of targets ){
			const to = graph.nodes.find( n=>n.id==id );
			if( !to )
				return `A wire goes to '${id}', which is not a node of the graph.`;
			if( !flowNodeType( to.type )?.input )
				return `A wire goes to '${id}', and a '${to.type}' node takes no input.`;
		}
	}
	return undefined;
}

//A node dropped from the palette:  an id the editor makes once and the graph keeps, so a trace written under one version reads
//under the next, and its type's first props.
export function newFlowNode( type:string, x:number, y:number, id:string=newFlowNodeId() ):FlowNode{
	const props = flowNodeType( type )?.defaults?.() ?? {};
	return { id, type, name: '', x, y, props, wires: Array.from( {length: flowOutputs( {type, props} )}, ()=>[] ) };
}
export function newFlowNodeId():string{ return `n${crypto.randomUUID().replace( /-/g, '' ).substring( 0, 10 )}`; }
//A graph as the canvas holds one, so two are compared like for like:  each node's wires a list per port it has, or is wired from,
//a target once a port, and none that is no node of the graph, which has no connector to end at.
export function normalizeFlowGraph( graph:FlowGraph ):FlowGraph{
	const ids = new Set( graph.nodes.map( n=>n.id ) );
	return { nodes: graph.nodes.map( n=>{
		const wires = n.wires.map( port=>[...new Set( port.filter( id=>ids.has( id ) ) )] );
		let ports = wires.length;
		const outputs = flowOutputs( n );
		while( ports>outputs && !wires[ports-1].length )
			--ports;
		return { ...n, wires: Array.from( {length: Math.max( outputs, ports )}, ( _, i )=>wires[i] ?? [] ) };
	} ) };
}

//What a side form changes of a node:  its name, its props, and for a node whose ports follow its props, a switch's rules, where
//each port it has now was before, -1 for a new one, so its wires follow their rules.
export type FlowNodePatch = { name?:string; props?:Record<string,unknown>; ports?:number[] };

//A field's value as its input shows it:  blank where the key is absent.
export function flowFieldText( f:FlowField, v:unknown ):string{
	if( absent( v ) )
		return f.kind=='json' && v===null ? 'null' : '';
	if( f.kind=='json' )
		return isObject( v ) || Array.isArray( v ) ? JSON.stringify( v, undefined, 2 ) : JSON.stringify( v );
	if( f.kind=='list' )
		return Array.isArray( v ) ? v.map( x=>typeof x=='string' ? x : JSON.stringify( x ) ).join( '\n' ) : String( v );
	return typeof v=='string' ? v : typeof v=='number' || typeof v=='boolean' ? String( v ) : JSON.stringify( v );
}
//What was typed into a field as its value, none for a field left blank, which takes its key away:  text that is no number stays
//text for a number, so its fault says so;  what is no json is text for a literal;  a list is a text a line.
export function flowFieldValue( f:FlowField, text:string ):unknown{
	const t = text.trim();
	switch( f.kind ){
	case 'number':
	case 'count':
		return !t ? undefined : Number.isFinite( Number( t ) ) ? Number( t ) : t;
	case 'json':
		if( !t )
			return undefined;
		try{
			return JSON.parse( t );
		}
		catch{
			return t;
		}
	case 'code':
		return text || f.required ? text : undefined;
	case 'list':{
		const items = text.split( f.item=='node' ? /\n/ : /[\n,]/ ).map( x=>x.trim() ).filter( x=>!!x );
		return items.length ? items : undefined;
	}
	default:
		if( f.orObject && t.startsWith( '{' ) ){
			try{
				const o = JSON.parse( t );
				if( isObject( o ) )
					return o;
			}
			catch{}
		}
		return t || undefined;
	}
}
//`o` with a key set, or taken away by none
export function flowWithField( o:Props, key:string, value:unknown ):Props{
	const y = {...o};
	if( value===undefined )
		delete y[key];
	else
		y[key] = value;
	return y;
}
//a rule with the keys its fields name alone:  what another `t` left behind goes
export function flowPruneRule( rule:Props, fields:( rule:Props )=>FlowField[] ):Props{
	const keys = new Set( fields( rule ).map( f=>f.key ) );
	return Object.fromEntries( Object.entries( rule ).filter( ( [key] )=>keys.has( key ) ) );
}