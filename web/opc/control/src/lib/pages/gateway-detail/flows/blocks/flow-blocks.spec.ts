import { flowBlocksCode, flowToolbox, newFlowBlocks } from './flow-blocks';

//A workspace as the editor saved it:  a branch on the payload against a context key, a property set, a status, a context set
//and a send down the second output in its arms, then a warning and the return.  Its one variable is named `msg`, which the
//body has already.
const saved = {
	blocks: { languageVersion: 0, blocks: [{
		type: 'controls_if', id: 'if', x: 24, y: 24, extraState: {hasElse: true},
		inputs: {
			IF0: {block: {type: 'logic_compare', id: 'cmp', fields: {OP: 'GT'}, inputs: {
				A: {block: {type: 'flow_msg_get', id: 'rpm', fields: {PATH: 'payload.rpm'}}},
				B: {block: {type: 'flow_context_get', id: 'limit', fields: {SCOPE: 'flow', KEY: 'limit'}}}
			}}},
			DO0: {block: {type: 'flow_msg_set', id: 'high', fields: {PATH: 'topic'}, inputs: {VALUE: {block: {type: 'text', id: 't', fields: {TEXT: "it's high"}}}},
				next: {block: {type: 'flow_status', id: 'red', fields: {FILL: 'red', SHAPE: 'ring'}, inputs: {TEXT: {block: {type: 'flow_msg_get', id: 'p', fields: {PATH: 'payload.rpm'}}}}}}
			}},
			ELSE: {block: {type: 'flow_context_set', id: 'seen', fields: {SCOPE: 'context', KEY: 'seen'}, inputs: {VALUE: {block: {type: 'variables_get', id: 'n', fields: {VAR: {id: 'v'}}}}},
				next: {block: {type: 'flow_send', id: 'second', fields: {OUTPUT: 2}, inputs: {VALUE: {block: {type: 'flow_msg', id: 'm1'}}}}}
			}}
		},
		next: {block: {type: 'flow_log', id: 'warn', fields: {LEVEL: 'warn'}, inputs: {TEXT: {block: {type: 'flow_env', id: 'site', fields: {NAME: 'SITE'}}}},
			next: {block: {type: 'flow_return', id: 'ret', inputs: {VALUE: {block: {type: 'flow_msg', id: 'm2'}}}}}
		}}
	}] },
	variables: [{name: 'msg', id: 'v'}]
};
const code = `var msg2;


__block('if');
if (msg.payload?.rpm > flow.get('limit')) {
  __block('high');
  msg.topic = 'it\\'s high';
  __block('red');
  node.status({fill: 'red', shape: 'ring', text: msg.payload?.rpm});
} else {
  __block('seen');
  context.set('seen', msg2);
  __block('second');
  node.send([null, msg]);
}
__block('warn');
node.warn(env.get('SITE'));
__block('ret');
return msg;`;
const one = ( block:object )=>flowBlocksCode( {blocks: {languageVersion: 0, blocks: [{id: 'b', ...block}]}} );
const called = "__block('b');\n";//a value left on its own is a statement too

//The code the generator writes of a workspace (#458):  what the Function node's `code` holds beside its `blocks`.
describe( 'flow blocks', ()=>{
	it( 'writes the code of a saved workspace, a call a statement', ()=>{
		expect( flowBlocksCode( saved ) ).toBe( code );
		expect( flowBlocksCode( saved ) ).toBe( code );//and again, of a workspace of its own
	} );
	//the code run as the sandbox runs it, a function of the message:  the calls are the statements of the arm taken
	it( 'calls the blocks of the branch a message takes', ()=>{
		const run = ( rpm:number )=>{
			const passed:string[] = [], sent:unknown[] = [];
			const node = { status: vi.fn(), warn: vi.fn(), send: ( m:unknown )=>sent.push( m ) }, store = { get: ()=>1400, set: vi.fn() };
			const msg = new Function( 'msg', 'node', '__block', 'context', 'flow', 'env', code )( {payload: {rpm}}, node, ( id:string )=>passed.push( id ), store, store, {get: ()=>'plant 3'} );
			return { passed, msg, sent, status: node.status.mock.calls, warned: node.warn.mock.calls };
		};
		const high = run( 1500 ), low = run( 10 );
		expect( high.passed ).toEqual( ['if', 'high', 'red', 'warn', 'ret'] );
		expect( high.msg ).toEqual( {payload: {rpm: 1500}, topic: "it's high"} );
		expect( high.status ).toEqual( [[{fill: 'red', shape: 'ring', text: 1500}]] );
		expect( low.passed ).toEqual( ['if', 'seen', 'second', 'warn', 'ret'] );
		expect( low.sent ).toEqual( [[null, {payload: {rpm: 10}}]] );
		expect( low.warned ).toEqual( [['plant 3']] );
	} );
	it( 'starts a new body as the message returned', ()=>{
		expect( flowBlocksCode( newFlowBlocks() ) ).toMatch( /^__block\('[^']{20}'\);\nreturn msg;$/ );
	} );
	it( "reads a message's property by its path", ()=>{
		const get = ( path:string )=>one( {type: 'flow_msg_get', fields: {PATH: path}} ), set = ( path:string )=>one( {type: 'flow_msg_set', fields: {PATH: path}} );
		expect( [get( 'payload' ), get( 'payload.mean' ), get( 'payload.items.0.name' ), get( 'payload.a b' )] )
			.toEqual( ['msg.payload;', 'msg.payload?.mean;', 'msg.payload?.items?.[0]?.name;', "msg.payload?.['a b'];"].map( x=>called+x ) );
		expect( [set( 'payload.items.0' ), set( ' topic ' )] ).toEqual( ['msg.payload.items[0] = null;', 'msg.topic = null;'].map( x=>called+x ) );
		expect( get( ' . ' ) ).toBe( called+'msg.payload;' );//no path is refused, and the field keeps its own
	} );
	it( 'writes each of its own blocks left empty', ()=>{
		expect( one( {type: 'flow_return'} ) ).toBe( called+"return null;" );
		expect( one( {type: 'flow_send'} ) ).toBe( called+"node.send(null);" );
		expect( one( {type: 'flow_send', fields: {OUTPUT: 3}} ) ).toBe( called+"node.send([null, null, null]);" );
		expect( one( {type: 'flow_context_set', fields: {SCOPE: 'global', KEY: 'k'}} ) ).toBe( called+"global.set('k', undefined);" );
		expect( one( {type: 'flow_error'} ) ).toBe( called+"node.error('', msg);" );
		expect( one( {type: 'flow_log'} ) ).toBe( called+"node.log('');" );
		expect( one( {type: 'flow_context_get'} ) ).toBe( called+"context.get('key');" );
	} );
	//each entry is a block the workspace takes, with a generator:  a stock block renamed by a Blockly upgrade fails here
	it( 'offers blocks the generator writes', ()=>{
		const entries = flowToolbox.contents.flatMap( c=>(<{ contents?:{ kind:string; type:string }[] }>c).contents ?? [] );
		expect( entries.length ).toBeGreaterThan( 60 );
		expect( entries.filter( ( { kind, ...entry } )=>{ try{ return !one( entry ); } catch{ return true; } } ).map( e=>e.type ) ).toEqual( [] );
		expect( flowToolbox.contents.map( c=>(<{ name?:string }>c).name ).filter( n=>!!n ) ).toEqual( ['Message', 'Context', 'Node', 'Logic', 'Math', 'Text', 'Lists', 'Variables'] );
	} );
	it( 'refuses what is no workspace', ()=>{
		expect( ()=>one( {type: 'flow_nothing'} ) ).toThrow();
	} );
} );