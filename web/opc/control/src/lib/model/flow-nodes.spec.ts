import { FlowGraph, FlowNode } from './flow';
import { flowFieldText, flowFieldValue, flowGraphFault, flowNodeFault, flowNodeFaults, flowNodeType, flowNodeTypes, flowOutputs, flowPathFault, flowPruneRule, flowWaitSeconds, flowWithField, newFlowNode, normalizeFlowGraph } from './flow-nodes';

const node = ( id:string, type:string, props:Record<string,unknown>={}, wires:string[][]=[] ):FlowNode=>({ id, type, name: '', x: 0, y: 0, props, wires });
const fault = ( type:string, props:Record<string,unknown>, graph?:FlowGraph )=>{ const n = node( 'n', type, props ); return flowNodeFault( n, graph ?? {nodes: [n]} ); };

//The editor's rules against the gateway's (libs/flows Graph.cpp `_types`, Nodes.cpp Outputs, src/nodes, FlowNodes::Check):  the
//types, their ports, and what each refuses.
describe( 'flow node types', ()=>{
	it( "are the library's, with its ports", ()=>{
		//name, takes an input:  Graph.cpp's table, in ENodeType's order
		const library:[string,boolean][] = [
			['opc in', false], ['group in', false], ['timer', false], ['calendar', false], ['connection', false], ['inject', false], ['link in', false],
			['switch', true], ['change', true], ['filter', true], ['range', true], ['delay', true], ['trigger', true], ['window', true], ['split', true],
			['join', true], ['template', true], ['function', true], ['debug', true], ['catch', false], ['status', false], ['complete', false], ['link out', true],
			['comment', false], ['opc out', true], ['variable', true], ['log', true], ['http request', true]
		];
		expect( flowNodeTypes.map( t=>[t.type, t.input] ) ).toEqual( library );
		const noOutput = ['debug', 'link out', 'comment', 'variable', 'log'];//Nodes.cpp Outputs
		for( const type of flowNodeTypes.filter( t=>t.type!='switch' ) )
			expect( [type.type, type.outputs( {} )] ).toEqual( [type.type, noOutput.includes( type.type ) ? 0 : 1] );
		expect( flowOutputs( node( 's', 'switch', {rules: [{t: 'true'}, {t: 'false'}, {t: 'else'}]} ) ) ).toBe( 3 );
		expect( flowOutputs( node( 's', 'switch' ) ) ).toBe( 0 );
		expect( [undefined, 0, 3, 101, 1.5].map( outputs=>flowOutputs( node( 'f', 'function', {outputs} ) ) ) ).toEqual( [1, 0, 3, 1, 1] );
		expect( flowOutputs( node( 'x', 'nothing' ) ) ).toBe( 0 );
	} );
	//a node as the palette drops it:  what its type must be told is said, and nothing else
	it( 'drops a node with its first props', ()=>{
		const dropped = Object.fromEntries( flowNodeTypes.map( t=>{ const n = newFlowNode( t.type, 10, 20, 'n' ); return [t.type, flowNodeFault( n, {nodes: [n]} )]; } ) );
		expect( Object.entries( dropped ).filter( ( [, f] )=>f ) ).toEqual( [
			['opc in', "`connection` is required, a connection's slug."],
			['group in', "`group` is required, a historian group's slug or id."],
			['calendar', "`calendar` is required, a calendar's slug or id."],
			['connection', "`connection` is required, a connection's slug."],
			['link in', '`name` must be text.'],
			['complete', '`scope` must name the nodes a complete node takes from.'],
			['link out', '`name` must be text.'],
			['opc out', "`connection` is required, a connection's slug."],
			['variable', '`name` must be text.']
		] );
		const n = newFlowNode( 'switch', 10, 20 );
		expect( n ).toMatchObject( {type: 'switch', name: '', x: 10, y: 20, wires: [[], []]} );
		expect( n.id ).toMatch( /^n[0-9a-f]{10}$/ );
	} );
} );

describe( 'flow node props', ()=>{
	it( "reads a text, a number, a flag, a choice and a wait as the library's Prop does", ()=>{
		expect( fault( 'filter', {property: ''} ) ).toBe( '`property` must be text.' );//an empty text where a key is present is refused
		expect( fault( 'filter', {property: null} ) ).toBeUndefined();//null is absent
		expect( fault( 'filter', {band: '1'} ) ).toBe( '`band` must be a number.' );
		expect( fault( 'filter', {perTopic: 'yes'} ) ).toBe( '`perTopic` must be true or false.' );
		expect( fault( 'range', {minIn: 0, maxIn: 1, minOut: 0, maxOut: 1, action: 'bend'} ) ).toBe( '`action` must be one of scale, clamp, wrap.' );
		expect( fault( 'delay', {} ) ).toBe( '`duration` must be an ISO 8601 duration above zero, as PT30S is.' );
		expect( fault( 'delay', {duration: 'PT0S'} ) ).toBe( '`duration` must be an ISO 8601 duration above zero, as PT30S is.' );
		expect( fault( 'delay', {duration: '30s'} ) ).toBe( '`duration` must be an ISO 8601 duration above zero, as PT30S is.' );
		expect( fault( 'delay', {duration: 'PT30S'} ) ).toBeUndefined();
		expect( fault( 'delay', {mode: 'rate', per: 'PT1S', count: 0} ) ).toBe( '`count` must be a whole number above zero.' );
		expect( fault( 'delay', {mode: 'rate', count: 5} ) ).toBe( '`per` must be an ISO 8601 duration above zero, as PT30S is.' );
		expect( fault( 'group in', {group: 'pumps', lateness: 'PT0S'} ) ).toBeUndefined();//zero is one there
		expect( [flowWaitSeconds( 'PT1M30S' ), flowWaitSeconds( 'P1D' ), flowWaitSeconds( 'P' ), flowWaitSeconds( '' ), flowWaitSeconds( 5 )] ).toEqual( [90, 86400, undefined, undefined, undefined] );
	} );
	it( "holds a property's path to Flows::Path", ()=>{
		expect( ['payload', 'payload.rpm', 'parts[0].id', 'a.b[2][3]'].map( flowPathFault ) ).toEqual( [undefined, undefined, undefined, undefined] );
		expect( flowPathFault( '' ) ).toContain( 'it is empty' );
		expect( flowPathFault( 'a..b' ) ).toContain( 'a name is missing' );
		expect( flowPathFault( '[0]' ) ).toContain( 'an index is a number in brackets after a name' );
		expect( flowPathFault( 'a[x]' ) ).toContain( 'an index is a number' );
		expect( flowPathFault( 'a[0]b' ) ).toContain( "a name follows a '.'" );
		expect( fault( 'filter', {property: 'a.'} ) ).toContain( "'a.' is not a property's path" );
	} );
	it( "holds a switch's and a change's rules to their fields", ()=>{
		expect( fault( 'switch', {} ) ).toBe( '`rules` must be a list of rules, one at least.' );
		expect( fault( 'switch', {rules: []} ) ).toBe( '`rules` must be a list of rules, one at least.' );
		expect( fault( 'switch', {rules: [{t: 'like'}]} ) ).toContain( 'Rule 1:  `t` must be one of eq, neq' );
		expect( fault( 'switch', {rules: [{t: 'eq'}]} ) ).toBe( 'Rule 1:  the rule has no `v`.' );
		expect( fault( 'switch', {rules: [{t: 'eq', v: null}]} ) ).toBeUndefined();//a literal null is a value
		expect( fault( 'switch', {rules: [{t: 'eq', v: 1}, {t: 'btwn', v: 1}]} ) ).toBe( 'Rule 2:  the rule has no `v2`.' );
		expect( fault( 'switch', {rules: [{t: 'gt', vt: 'flow'}]} ) ).toBe( 'Rule 1:  `v` must be text.' );//a context key
		expect( fault( 'switch', {rules: [{t: 'gt', vt: 'msg', v: 'a..b'}]} ) ).toContain( "is not a property's path" );
		expect( fault( 'switch', {rules: [{t: 'gt', vt: 'node', v: 'x'}]} ) ).toBe( 'Rule 1:  `vt` must be one of msg, flow, global.' );
		expect( fault( 'switch', {rules: [{t: 'istype', v: 'date'}]} ) ).toContain( '`v` must be one of string, number' );
		expect( fault( 'switch', {rules: [{t: 'regex', v: '(' }]} ) ).toContain( "'(' is not a regular expression" );
		expect( fault( 'switch', {rules: [{t: 'regex', v: '^a', case: true}, {t: 'true'}, {t: 'nempty'}, {t: 'else'}]} ) ).toBeUndefined();
		expect( fault( 'change', {rules: [{t: 'set', p: 'payload'}]} ) ).toBe( 'Rule 1:  the rule has no `to`.' );
		expect( fault( 'change', {rules: [{t: 'set', p: '_id', to: 1}]} ) ).toContain( "is the engine's" );
		expect( fault( 'change', {rules: [{t: 'set', p: '_id', pt: 'flow', to: 1}]} ) ).toBeUndefined();//a context key of that name is the flow's own
		expect( fault( 'change', {rules: [{t: 'move', p: 'a', to: '_parent.x'}]} ) ).toContain( "is the engine's" );
		expect( fault( 'change', {rules: [{t: 'delete', p: 'topic'}, {t: 'move', p: 'a', to: 'b', tot: 'global'}, {t: 'set', p: 'x', to: 'count', tot: 'flow'}]} ) ).toBeUndefined();
	} );
	it( "holds each library node to its own rule", ()=>{
		expect( fault( 'filter', {percent: true} ) ).toBe( '`percent` is of a `band`.' );
		expect( fault( 'filter', {band: -1} ) ).toBe( '`band` must not be below zero.' );
		expect( fault( 'range', {minIn: 1, maxIn: 1, minOut: 0, maxOut: 1} ) ).toContain( 'the input range is empty' );
		expect( fault( 'range', {minIn: 0, maxIn: 1, minOut: 0} ) ).toBe( '`maxOut` must be a number.' );
		expect( fault( 'trigger', {wait: 'PT30S'} ) ).toBe( 'a trigger sends a `first`, a `second` or both.' );
		expect( fault( 'trigger', {wait: 'PT30S', second: 'late'} ) ).toBeUndefined();
		expect( fault( 'window', {} ) ).toBe( 'a window is by `duration` or by `count`, one of the two.' );
		expect( fault( 'window', {duration: 'PT1M', count: 5} ) ).toBe( 'a window is by `duration` or by `count`, one of the two.' );
		expect( fault( 'window', {count: 5, kind: 'sliding'} ) ).toBeUndefined();
		expect( fault( 'join', {mode: 'array'} ) ).toBe( 'a join into an array or an object needs a `count` or a `timeout`.' );
		expect( fault( 'join', {mode: 'object', timeout: 'PT5S'} ) ).toBeUndefined();
		expect( fault( 'join', {} ) ).toBeUndefined();
		expect( fault( 'split', {} ) ).toBeUndefined();
		expect( fault( 'template', {} ) ).toBe( '`template` must be text.' );
		expect( fault( 'template', {template: ''} ) ).toBeUndefined();//text, which may be empty
		expect( fault( 'template', {template: 'rpm {{payload'} ) ).toBe( 'the template has a tag that does not close.' );
		expect( fault( 'template', {template: '{{#each}}'} ) ).toContain( "its variables alone" );
		expect( fault( 'template', {template: '{{! a comment }}{{{raw}}} {{&payload}}'} ) ).toBeUndefined();
		expect( fault( 'function', {} ) ).toBe( '`code` must be text, the body.' );
		expect( fault( 'function', {code: '', outputs: 101} ) ).toBe( '`outputs` must be a whole number, 100 at most.' );
		expect( fault( 'function', {code: 'return msg;', onStart: 3} ) ).toBe( '`onStart` must be text.' );
		expect( fault( 'debug', {level: 'Loud'} ) ).toContain( '`level` must be one of Trace' );
		expect( fault( 'debug', {} ) ).toBeUndefined();
		expect( fault( 'inject', {message: 5} ) ).toBe( '`message` must be an object, the message the node sends.' );
		expect( fault( 'inject', {} ) ).toBeUndefined();
		expect( fault( 'comment', {} ) ).toBeUndefined();
		expect( fault( 'nothing', {} ) ).toBe( '`type` "nothing" is not a node type.' );
	} );
	it( "holds a catch's, a status's and a complete's scope to the graph's nodes", ()=>{
		const a = node( 'a', 'function', {code: ''} );
		const scoped = ( type:string, scope:unknown )=>{ const n = node( 'n', type, {scope} ); return flowNodeFault( n, {nodes: [a, n]} ); };
		expect( scoped( 'catch', ['a'] ) ).toBeUndefined();
		expect( scoped( 'catch', undefined ) ).toBeUndefined();//every node
		expect( scoped( 'status', ['gone'] ) ).toBe( '`scope` names "gone", which is not a node of the graph.' );
		expect( scoped( 'catch', 'a' ) ).toBe( '`scope` must be a list of node ids.' );
		expect( scoped( 'complete', [] ) ).toBe( '`scope` must name the nodes a complete node takes from.' );
		expect( scoped( 'complete', ['a'] ) ).toBeUndefined();
		expect( fault( 'catch', {uncaught: 1} ) ).toBe( '`uncaught` must be true or false.' );
	} );
	it( "holds each hosted node to the gateway's check", ()=>{
		const target = { connection: 'local', node: 'ns=2;s=Pump1.Rpm' };
		expect( fault( 'opc in', target ) ).toBeUndefined();
		expect( fault( 'opc in', {connection: 'local', node: {nsu: 'urn:jde:pumps', s: 'Pump1.Rpm'}} ) ).toBeUndefined();
		expect( fault( 'opc in', {connection: 'local'} ) ).toBe( "`node` is required, the node's id." );
		expect( fault( 'opc in', {connection: 'local', node: 'rpm'} ) ).toContain( 'is not a node id' );
		expect( fault( 'opc in', {...target, queueSize: 1.5} ) ).toBe( '`queueSize` must be a whole number.' );
		expect( fault( 'opc in', {...target, filter: {trigger: 'value'}} ) ).toContain( '`trigger` must be one of status, statusValue' );
		expect( fault( 'opc in', {...target, filter: {deadbandType: 'absolute', deadbandValue: -1}} ) ).toBe( '`deadbandValue` must not be negative.' );
		expect( fault( 'opc in', {...target, filter: 5} ) ).toBe( '`filter` must be an object of trigger, deadbandType, deadbandValue.' );
		expect( fault( 'opc in', {...target, samplingInterval: 0, queueSize: 10, discardOldest: false, filter: {deadbandType: 'percent', deadbandValue: 5}} ) ).toBeUndefined();
		expect( fault( 'opc out', {...target, property: 'payload.value'} ) ).toBeUndefined();
		expect( fault( 'group in', {group: 7, from: 'now', members: ['ns=2;i=5']} ) ).toBeUndefined();
		expect( fault( 'group in', {group: 'pumps', from: 'yesterday'} ) ).toContain( '`from` must be resume, now or a time' );
		expect( fault( 'group in', {group: 'pumps', from: '2026-03-09T06:00:00Z', modifications: true} ) ).toBeUndefined();
		expect( fault( 'group in', {group: 'pumps', members: 'ns=2;i=5'} ) ).toBe( '`members` must be a list.' );
		expect( fault( 'group in', {group: 'pumps', members: ['rpm']} ) ).toBe( '"rpm" is not a node id.' );
		expect( fault( 'timer', {} ) ).toBe( 'A timer takes `every`, an ISO 8601 duration, or `at`, local times.' );
		expect( fault( 'timer', {every: 'PT1M', at: {times: ['06:00']}} ) ).toBe( 'A timer takes `every` or `at`, not both.' );
		expect( fault( 'timer', {every: 'PT0.0001S'} ) ).toBe( '`every` must be a millisecond at least.' );
		expect( fault( 'timer', {at: {}} ) ).toBe( '`at.times` must be a list of local times, as 06:00.' );
		expect( fault( 'timer', {at: {times: ['6am']}} ) ).toContain( 'which is not a time of day' );
		expect( fault( 'timer', {at: {times: ['24:00']}} ) ).toContain( 'which is not a time of day' );
		expect( fault( 'timer', {at: {times: ['06:00'], days: ['monday']}} ) ).toContain( 'which is not one of sun, mon' );
		expect( fault( 'timer', {at: {times: ['06:00'], days: []}} ) ).toBe( '`at.days` names no day.' );
		expect( fault( 'timer', {at: {times: ['06:00', '14:30:15'], days: ['mon', 'fri'], zone: 'Europe/Berlin'}} ) ).toBeUndefined();
		expect( fault( 'calendar', {calendar: 3, boundaries: ['shiftStart', 'lunch']} ) ).toContain( 'which is not one of shiftStart' );
		expect( fault( 'calendar', {calendar: 'plant', boundaries: ['shiftStart', 'breakEnd']} ) ).toBeUndefined();
		expect( fault( 'connection', {connection: 'local'} ) ).toBeUndefined();
		expect( fault( 'log', {} ) ).toBeUndefined();
		expect( fault( 'log', {template: ''} ) ).toBe( '`template` must be text.' );
		expect( fault( 'log', {template: '{{payload', level: 'Warning'} ) ).toBe( 'the template has a tag that does not close.' );
		expect( fault( 'http request', {} ) ).toBeUndefined();//the message's url
		expect( fault( 'http request', {url: 'ftp://plant/x'} ) ).toBe( "'ftp://plant/x' is not an http or https url." );
		expect( fault( 'http request', {url: 'https://ann@plant/x'} ) ).toContain( 'carries a user' );
		expect( fault( 'http request', {url: 'http:///x'} ) ).toContain( 'names no host' );
		expect( fault( 'http request', {url: 'http://plant:99999/x'} ) ).toContain( 'has a port that is not one' );
		expect( fault( 'http request', {url: 'https://[::1]:8443/hook', method: 'post', output: 'json', contentType: 'text/plain'} ) ).toBeUndefined();
		expect( fault( 'http request', {method: 'FETCH'} ) ).toBe( "'FETCH' is not an http method." );
	} );
	it( 'holds a variable to the flow server', ()=>{
		const v = { name: 'rpmMean', dataType: 'Double' };
		expect( fault( 'variable', v ) ).toBeUndefined();
		expect( fault( 'variable', {...v, dataType: 'double[]', description: 'Mean', engineeringUnits: 'rpm', euRange: {low: 0, high: 3000}} ) ).toBeUndefined();
		expect( fault( 'variable', {...v, engineeringUnits: {displayName: 'rpm', unitId: 5}} ) ).toBeUndefined();
		expect( fault( 'variable', {name: 'rpmMean'} ) ).toBe( '`dataType` must be text.' );
		expect( fault( 'variable', {...v, name: 'pump1/rpmMean'} ) ).toContain( "holds a '/'" );
		expect( fault( 'variable', {...v, name: '_Flow'} ) ).toContain( "is the folder's own object" );
		expect( fault( 'variable', {...v, dataType: 'Real'} ) ).toContain( "`dataType` 'Real' is no built-in type" );
		expect( fault( 'variable', {...v, description: ''} ) ).toBe( '`description` must be text.' );
		expect( fault( 'variable', {...v, engineeringUnits: ''} ) ).toBe( '`engineeringUnits` must be text.' );
		expect( fault( 'variable', {...v, euRange: {low: 5, high: 5}} ) ).toBe( "`euRange`'s low must be under its high." );
		expect( fault( 'variable', {...v, euRange: {low: 5}} ) ).toBe( '`high` must be a number.' );
		const twice:FlowGraph = { nodes: [node( 'a', 'variable', v ), node( 'b', 'variable', v )] };
		expect( flowNodeFault( twice.nodes[1], twice ) ).toBe( "Two variable nodes are named 'rpmMean'." );
	} );
} );

describe( "a flow graph's faults", ()=>{
	const fn = ( id:string, wires:string[][]=[[]] )=>node( id, 'function', {code: 'return msg;'}, wires );
	it( 'finds the first, in node order, as Graph::Read does', ()=>{
		expect( flowGraphFault( {nodes: []} ) ).toBeUndefined();
		expect( flowGraphFault( {nodes: [fn( 'a' ), node( '', 'debug' )]} ) ).toEqual( {message: 'Node 2 of the graph has no `id`.'} );
		expect( flowGraphFault( {nodes: [fn( 'a' ), fn( 'a' )]} ) ).toEqual( {node: 'a', message: 'Two nodes have this id.'} );
		expect( flowGraphFault( {nodes: [node( 'a', 'http in' )]} ) ).toEqual( {node: 'a', message: '`type` "http in" is not a node type.'} );
		expect( flowGraphFault( {nodes: [node( 'a', '' )]} ) ).toEqual( {node: 'a', message: '`type` is missing, and so is not a node type.'} );
		expect( flowGraphFault( {nodes: [fn( 'a', [['b']] ), node( 'b', 'filter', {band: -1} ), node( 'c', 'window' )]} ) ).toEqual( {node: 'b', message: '`band` must not be below zero.'} );
	} );
	it( 'holds the wires to the ports', ()=>{
		expect( flowGraphFault( {nodes: [fn( 'a', [['gone']] )]} ) ).toEqual( {node: 'a', message: "A wire goes to 'gone', which is not a node of the graph." } );
		expect( flowGraphFault( {nodes: [fn( 'a', [['in']] ), node( 'in', 'inject' )]} ) ).toEqual( {node: 'a', message: "A wire goes to 'in', and a 'inject' node takes no input."} );
		expect( flowGraphFault( {nodes: [fn( 'a', [[], ['b']] ), fn( 'b' )]} ) ).toEqual( {node: 'a', message: "Output 2 is wired, and a 'function' node has 1."} );
		expect( flowGraphFault( {nodes: [fn( 'a', [['b'], []] ), fn( 'b' )]} ) ).toBeUndefined();//a port past the last, unwired, is nothing
		expect( flowGraphFault( {nodes: [node( 'd', 'debug', {}, [['a']] ), fn( 'a' )]} ) ).toEqual( {node: 'd', message: "Output 1 is wired, and a 'debug' node has 0."} );
		expect( flowGraphFault( {nodes: [fn( 'a', [['a']] )]} ) ).toBeUndefined();//a loop is the flow's own business
	} );
	//a link out with no link in here may send to another flow's, which the gateway knows:  two link ins of a name are a fault
	it( 'holds the links to their names', ()=>{
		const linkIn = ( id:string, name:string )=>node( id, 'link in', {name}, [[]] );
		expect( flowGraphFault( {nodes: [fn( 'a', [['o']] ), node( 'o', 'link out', {name: 'elsewhere'} )]} ) ).toBeUndefined();
		expect( flowGraphFault( {nodes: [linkIn( 'a', 'x' ), linkIn( 'b', 'x' )]} ) ).toEqual( {node: 'a', message: "Two `link in` nodes are named 'x'."} );
		expect( flowGraphFault( {nodes: [linkIn( 'a', 'x' ), linkIn( 'b', 'y' ), node( 'o', 'link out', {name: 'x'} )]} ) ).toBeUndefined();
	} );
	it( "maps each node's fault for the canvas", ()=>{
		const graph:FlowGraph = { nodes: [fn( 'a', [['gone']] ), node( 'b', 'window' ), fn( 'c' )] };
		expect( [...flowNodeFaults( graph )] ).toEqual( [['a', "A wire goes to 'gone', which is not a node of the graph."], ['b', 'a window is by `duration` or by `count`, one of the two.']] );
	} );
	//the form the canvas gives a graph back in, so a graph just opened is not one changed
	it( 'normalizes the wires to the ports', ()=>{
		const graph:FlowGraph = { nodes: [node( 'a', 'function', {code: ''} ), node( 's', 'switch', {rules: [{t: 'true'}, {t: 'else'}]}, [['a', 'a', 'gone'], [], ['a'], []] ), node( 'd', 'debug', {}, [[]] )] };
		expect( normalizeFlowGraph( graph ).nodes.map( n=>n.wires ) ).toEqual( [[[]], [['a'], [], ['a']], []] );
	} );
} );

describe( 'flow form values', ()=>{
	const field = ( type:string, key:string, props:Record<string,unknown>={} )=>flowNodeType( type )!.fields( props ).find( f=>f.key==key )!;
	it( 'reads what was typed as the field takes it', ()=>{
		expect( flowFieldValue( field( 'filter', 'band' ), ' 2.5 ' ) ).toBe( 2.5 );
		expect( flowFieldValue( field( 'filter', 'band' ), '' ) ).toBeUndefined();//blank takes the key away
		expect( flowFieldValue( field( 'filter', 'band' ), 'two' ) ).toBe( 'two' );//kept, for its fault to say
		expect( flowFieldValue( field( 'filter', 'property' ), ' payload.rpm ' ) ).toBe( 'payload.rpm' );
		expect( flowFieldValue( field( 'trigger', 'second' ), '{"late":true}' ) ).toEqual( {late: true} );
		expect( flowFieldValue( field( 'trigger', 'second' ), 'no value for 30 s' ) ).toBe( 'no value for 30 s' );//what is no json is text
		expect( flowFieldValue( field( 'trigger', 'second' ), 'null' ) ).toBeNull();
		expect( flowFieldValue( field( 'function', 'code' ), '' ) ).toBe( '' );//a body may be empty
		expect( flowFieldValue( field( 'function', 'onStart' ), '' ) ).toBeUndefined();
		expect( flowFieldValue( field( 'timer', 'at' ).fields!( {} )[0], '06:00, 14:30\n22:00' ) ).toEqual( ['06:00', '14:30', '22:00'] );
		expect( flowFieldValue( field( 'group in', 'members' ), 'ns=2;i=5\n{"nsu":"urn:x","i":6}' ) ).toEqual( ['ns=2;i=5', '{"nsu":"urn:x","i":6}'] );
		expect( flowFieldValue( field( 'variable', 'engineeringUnits' ), '{"displayName":"rpm"}' ) ).toEqual( {displayName: 'rpm'} );
		expect( flowFieldValue( field( 'variable', 'engineeringUnits' ), 'rpm' ) ).toBe( 'rpm' );
	} );
	//a row given by its id stays one where it is edited, since the gateway reads text as a slug (reviews-issue/455-flows-web-editor.md #6)
	it( "keeps a row's id a number", ()=>{
		expect( flowFieldValue( field( 'group in', 'group' ), ' 6 ', 5 ) ).toBe( 6 );
		expect( flowFieldValue( field( 'calendar', 'calendar' ), '3', 2 ) ).toBe( 3 );
		expect( flowFieldValue( field( 'group in', 'group' ), 'pumps', 5 ) ).toBe( 'pumps' );
		expect( flowFieldValue( field( 'group in', 'group' ), '6', 'pumps' ) ).toBe( '6' );//a slug of digits stays a slug
		expect( flowFieldValue( field( 'group in', 'group' ), '', 5 ) ).toBeUndefined();
	} );
	it( 'shows a value as its input holds it', ()=>{
		expect( flowFieldText( field( 'filter', 'band' ), 2.5 ) ).toBe( '2.5' );
		expect( flowFieldText( field( 'filter', 'band' ), undefined ) ).toBe( '' );
		expect( flowFieldText( field( 'trigger', 'second' ), 'late' ) ).toBe( '"late"' );
		expect( flowFieldText( field( 'trigger', 'second' ), null ) ).toBe( 'null' );
		expect( flowFieldText( field( 'inject', 'message' ), {payload: 1} ) ).toBe( '{\n  "payload": 1\n}' );
		expect( flowFieldText( field( 'group in', 'members' ), ['ns=2;i=5', {nsu: 'urn:x', i: 6}] ) ).toBe( 'ns=2;i=5\n{"nsu":"urn:x","i":6}' );
		expect( flowFieldText( field( 'opc in', 'node' ), {nsu: 'urn:x', i: 6} ) ).toBe( '{"nsu":"urn:x","i":6}' );
	} );
	it( 'sets a key, takes one away, and prunes a rule to its fields', ()=>{
		expect( flowWithField( {a: 1, b: 2}, 'a', undefined ) ).toEqual( {b: 2} );
		expect( flowWithField( {a: 1}, 'b', false ) ).toEqual( {a: 1, b: false} );
		const rules = field( 'switch', 'rules' ).fields!;
		expect( flowPruneRule( {t: 'true', v: 1, vt: 'msg', v2: 5}, rules ) ).toEqual( {t: 'true'} );
		expect( flowPruneRule( {t: 'btwn', v: 1, v2: 5, case: true}, rules ) ).toEqual( {t: 'btwn', v: 1, v2: 5} );
	} );
} );