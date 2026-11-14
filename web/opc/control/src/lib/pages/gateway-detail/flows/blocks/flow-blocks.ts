import * as Blockly from 'blockly/core';
import 'blockly/blocks';
import { javascriptGenerator, Order } from 'blockly/javascript';
import * as En from 'blockly/msg/en';

//The block editor of a Function node's body (#458, docs/flows/spec.md *The Function node*):  Blockly's stock logic, maths, text,
//lists and variables with the flow's own blocks, and the JavaScript the generator writes of a workspace, which is the node's
//`code`, its workspace `blocks` beside it.  Each statement's code begins `__block( id )`, as Blockly's own step debugger has it,
//which the sandbox keeps on the message's hop, so the history lights the blocks a message passed.  This file and
//flow-block-editor.ts alone import Blockly, and both load once a body's blocks are asked for (flow-blocks-loader.ts).

type Generated = [string, number];
const hue = { message: 20, context: 290, node: 345 };//beside the stock 210, 230, 160, 260 and 330

//`a.b.0` as its segments, none for text that is no path
const segments = ( path:string ):string[]=>path.split( '.' ).map( s=>s.trim() ).filter( s=>!!s );
//a member of what is before it:  `.mean`, `[0]`, `["a b"]`, and past the first of a read `?.`, so a property that is not there
//reads as undefined, where a body would throw
const member = ( segment:string, optional:boolean ):string=>
	/^[A-Za-z_$][\w$]*$/.test( segment ) ? `${optional ? '?.' : '.'}${segment}` : `${optional ? '?.' : ''}[${/^\d+$/.test( segment ) ? segment : javascriptGenerator.quote_( segment )}]`;
const messagePath = ( block:Blockly.Block, read:boolean ):string=>`msg${segments( block.getFieldValue( 'PATH' ) ).map( ( s, i )=>member( s, read && i>0 ) ).join( '' )}`;
const value = ( block:Blockly.Block, name:string, order:number, none='null' ):string=>javascriptGenerator.valueToCode( block, name, order ) || none;

const scopes = [['the node\'s', 'context'], ['the flow\'s', 'flow'], ['the gateway\'s', 'global']];
const definitions = [
	{ type: 'flow_msg', message0: 'msg', output: null, colour: hue.message, tooltip: 'The message the node received.' },
	{ type: 'flow_msg_get', message0: 'msg . %1', args0: [{type: 'field_input', name: 'PATH', text: 'payload', spellcheck: false}], output: null, colour: hue.message, extensions: ['flow_path'],
		tooltip: 'A property of the message, by its path:  payload, topic, payload.mean.' },
	{ type: 'flow_msg_set', message0: 'set msg . %1 to %2', args0: [{type: 'field_input', name: 'PATH', text: 'payload', spellcheck: false}, {type: 'input_value', name: 'VALUE'}],
		previousStatement: null, nextStatement: null, colour: hue.message, extensions: ['flow_path'], tooltip: 'Set a property of the message, by its path.' },
	{ type: 'flow_return', message0: 'return %1', args0: [{type: 'input_value', name: 'VALUE'}], previousStatement: null, colour: hue.message,
		tooltip: 'End the body:  the message it returns goes on, a list sends one an output, and nothing sends none.' },
	{ type: 'flow_send', message0: 'send %1 to output %2', args0: [{type: 'input_value', name: 'VALUE'}, {type: 'field_number', name: 'OUTPUT', value: 1, min: 1, max: 100, precision: 1}],
		previousStatement: null, nextStatement: null, colour: hue.message, inputsInline: true, tooltip: 'Send a message from inside the body, as node.send does:  a copy, under an id of its own.' },
	{ type: 'flow_context_get', message0: '%1 context %2', args0: [{type: 'field_dropdown', name: 'SCOPE', options: scopes}, {type: 'field_input', name: 'KEY', text: 'key', spellcheck: false}],
		output: null, colour: hue.context, tooltip: 'A key of a context:  the node\'s own, the flow\'s or the gateway\'s.' },
	{ type: 'flow_context_set', message0: 'set %1 context %2 to %3', args0: [{type: 'field_dropdown', name: 'SCOPE', options: scopes}, {type: 'field_input', name: 'KEY', text: 'key', spellcheck: false}, {type: 'input_value', name: 'VALUE'}],
		previousStatement: null, nextStatement: null, colour: hue.context, tooltip: 'Set a key of a context;  nothing takes the key away.' },
	{ type: 'flow_status', message0: 'status %1 %2 %3', args0: [
			{type: 'field_dropdown', name: 'FILL', options: [['green', 'green'], ['yellow', 'yellow'], ['red', 'red'], ['blue', 'blue'], ['grey', 'grey']]},
			{type: 'field_dropdown', name: 'SHAPE', options: [['dot', 'dot'], ['ring', 'ring']]}, {type: 'input_value', name: 'TEXT'}],
		previousStatement: null, nextStatement: null, colour: hue.node, inputsInline: true, tooltip: 'The node\'s status, under it on the canvas.' },
	{ type: 'flow_error', message0: 'error %1', args0: [{type: 'input_value', name: 'TEXT'}], previousStatement: null, nextStatement: null, colour: hue.node,
		tooltip: 'The node\'s error with the message, which the catch nodes take.' },
	{ type: 'flow_log', message0: '%1 %2', args0: [{type: 'field_dropdown', name: 'LEVEL', options: [['log', 'log'], ['warn', 'warn']]}, {type: 'input_value', name: 'TEXT'}],
		previousStatement: null, nextStatement: null, colour: hue.node, tooltip: 'A line in the gateway\'s log.' },
	{ type: 'flow_env', message0: 'env %1', args0: [{type: 'field_input', name: 'NAME', text: 'NR_FLOW_NAME', spellcheck: false}], output: null, colour: hue.node,
		tooltip: 'What the gateway\'s settings name for its flows, then NR_NODE_ID, NR_NODE_NAME and NR_FLOW_NAME.' }
];

const text = ( of:string )=>({ shadow: {type: 'text', fields: {TEXT: of}} });
const number = ( of:number )=>({ shadow: {type: 'math_number', fields: {NUM: of}} });
const message = { shadow: {type: 'flow_msg'} };
const block = ( type:string, more:object={} )=>({ kind: 'block', type, ...more });
//the stock categories the spec names, less what the sandbox has no window for, a print and a prompt
export const flowToolbox = {
	kind: 'categoryToolbox',
	contents: [
		{ kind: 'category', name: 'Message', colour: String( hue.message ), contents: [
			block( 'flow_msg_get', {fields: {PATH: 'payload'}} ), block( 'flow_msg_get', {fields: {PATH: 'topic'}} ),
			block( 'flow_msg_set', {fields: {PATH: 'payload'}} ), block( 'flow_msg_set', {fields: {PATH: 'topic'}, inputs: {VALUE: text( '' )}} ),
			block( 'flow_msg' ), block( 'flow_return', {inputs: {VALUE: message}} ), block( 'flow_send', {inputs: {VALUE: message}} )
		] },
		{ kind: 'category', name: 'Context', colour: String( hue.context ), contents: [
			block( 'flow_context_get' ), block( 'flow_context_get', {fields: {SCOPE: 'flow'}} ), block( 'flow_context_get', {fields: {SCOPE: 'global'}} ), block( 'flow_context_set' )
		] },
		{ kind: 'category', name: 'Node', colour: String( hue.node ), contents: [
			block( 'flow_status', {inputs: {TEXT: text( 'ok' )}} ), block( 'flow_error', {inputs: {TEXT: text( '' )}} ),
			block( 'flow_log', {fields: {LEVEL: 'warn'}, inputs: {TEXT: text( '' )}} ), block( 'flow_log', {inputs: {TEXT: text( '' )}} ), block( 'flow_env' )
		] },
		{ kind: 'sep' },
		{ kind: 'category', name: 'Logic', categorystyle: 'logic_category', contents: [
			block( 'controls_if' ), block( 'controls_if', {extraState: {hasElse: true}} ), block( 'logic_compare' ), block( 'logic_operation' ), block( 'logic_negate' ),
			block( 'logic_boolean' ), block( 'logic_null' ), block( 'logic_ternary' )
		] },
		{ kind: 'category', name: 'Math', categorystyle: 'math_category', contents: [
			block( 'math_number' ), block( 'math_arithmetic', {inputs: {A: number( 1 ), B: number( 1 )}} ), block( 'math_single', {inputs: {NUM: number( 9 )}} ),
			block( 'math_round', {inputs: {NUM: number( 3.1 )}} ), block( 'math_modulo', {inputs: {DIVIDEND: number( 64 ), DIVISOR: number( 10 )}} ),
			block( 'math_constrain', {inputs: {VALUE: number( 50 ), LOW: number( 1 ), HIGH: number( 100 )}} ), block( 'math_number_property', {inputs: {NUMBER_TO_CHECK: number( 0 )}} ),
			block( 'math_on_list' ), block( 'math_trig', {inputs: {NUM: number( 45 )}} ), block( 'math_constant' ),
			block( 'math_random_int', {inputs: {FROM: number( 1 ), TO: number( 100 )}} ), block( 'math_random_float' )
		] },
		{ kind: 'category', name: 'Text', categorystyle: 'text_category', contents: [
			block( 'text' ), block( 'text_join' ), block( 'text_length', {inputs: {VALUE: text( 'abc' )}} ), block( 'text_isEmpty', {inputs: {VALUE: text( '' )}} ),
			block( 'text_indexOf', {inputs: {FIND: text( 'abc' )}} ), block( 'text_charAt' ), block( 'text_getSubstring' ), block( 'text_changeCase', {inputs: {TEXT: text( 'abc' )}} ),
			block( 'text_trim', {inputs: {TEXT: text( 'abc' )}} ), block( 'text_count', {inputs: {SUB: text( '' ), TEXT: text( '' )}} ),
			block( 'text_replace', {inputs: {FROM: text( '' ), TO: text( '' ), TEXT: text( '' )}} ), block( 'text_reverse', {inputs: {TEXT: text( '' )}} ), block( 'text_append', {inputs: {TEXT: text( '' )}} )
		] },
		{ kind: 'category', name: 'Lists', categorystyle: 'list_category', contents: [
			block( 'lists_create_with', {extraState: {itemCount: 0}} ), block( 'lists_create_with' ), block( 'lists_repeat', {inputs: {NUM: number( 5 )}} ), block( 'lists_length' ), block( 'lists_isEmpty' ),
			block( 'lists_indexOf' ), block( 'lists_getIndex' ), block( 'lists_setIndex' ), block( 'lists_getSublist' ),
			block( 'lists_split', {inputs: {DELIM: text( ',' )}} ), block( 'lists_sort' ), block( 'lists_reverse' )
		] },
		{ kind: 'category', name: 'Variables', categorystyle: 'variable_category', custom: 'VARIABLE' }
	]
};

//A new body's workspace:  the message returned as it came, what a typed body starts as.
export const newFlowBlocks = ():object=>({ blocks: {languageVersion: 0, blocks: [{type: 'flow_return', x: 24, y: 24, inputs: {VALUE: {shadow: {type: 'flow_msg'}}}}]} });

let theme:Blockly.Theme|undefined;
//Once a page:  the messages, the flow's blocks and their code, the call a statement begins with, and the names a body has
//already, which a variable of the workspace is renamed past.
function setup():Blockly.Theme{
	if( theme )
		return theme;
	Blockly.setLocale( <any>En );
	Blockly.Extensions.register( 'flow_path', function( this:Blockly.Block ){
		this.getField( 'PATH' )!.setValidator( ( path:string )=>segments( path ).length ? segments( path ).join( '.' ) : null );
	} );
	Blockly.common.defineBlocksWithJsonArray( definitions );
	const g = javascriptGenerator, call = ( b:Blockly.Block, name:string )=>value( b, name, Order.NONE );
	g.STATEMENT_PREFIX = '__block(%1);\n';
	g.addReservedWords( 'msg,node,context,flow,global,env,RED,__block,setTimeout,setInterval,clearTimeout,clearInterval' );
	Object.assign( g.forBlock, <Record<string,( b:Blockly.Block )=>Generated|string>>{
		flow_msg: ()=>['msg', Order.ATOMIC],
		flow_msg_get: b=>[messagePath( b, true ), Order.MEMBER],
		flow_msg_set: b=>`${messagePath( b, false )} = ${value( b, 'VALUE', Order.ASSIGNMENT )};\n`,
		flow_return: b=>`return ${call( b, 'VALUE' )};\n`,
		flow_send: b=>{
			const output = Number( b.getFieldValue( 'OUTPUT' ) ), message = value( b, 'VALUE', output>1 ? Order.COMMA : Order.NONE );
			return `node.send(${output>1 ? `[${'null, '.repeat( output-1 )}${message}]` : message});\n`;
		},
		flow_context_get: b=>[`${b.getFieldValue( 'SCOPE' )}.get(${g.quote_( b.getFieldValue( 'KEY' ) )})`, Order.FUNCTION_CALL],
		flow_context_set: b=>`${b.getFieldValue( 'SCOPE' )}.set(${g.quote_( b.getFieldValue( 'KEY' ) )}, ${value( b, 'VALUE', Order.COMMA, 'undefined' )});\n`,
		flow_status: b=>`node.status({fill: ${g.quote_( b.getFieldValue( 'FILL' ) )}, shape: ${g.quote_( b.getFieldValue( 'SHAPE' ) )}, text: ${value( b, 'TEXT', Order.COMMA, "''" )}});\n`,
		flow_error: b=>`node.error(${value( b, 'TEXT', Order.COMMA, "''" )}, msg);\n`,
		flow_log: b=>`node.${b.getFieldValue( 'LEVEL' )}(${value( b, 'TEXT', Order.NONE, "''" )});\n`,
		flow_env: b=>[`env.get(${g.quote_( b.getFieldValue( 'NAME' ) )})`, Order.FUNCTION_CALL]
	} );
	//the page's colours, light or dark as its theme is:  each is set as a style, where a custom property reads
	return theme = Blockly.Theme.defineTheme( 'jde-flow', {
		name: 'jde-flow', base: Blockly.Themes.Classic,
		componentStyles: {
			workspaceBackgroundColour: 'var(--mat-sys-surface-container-low)', toolboxBackgroundColour: 'var(--mat-sys-surface-container)', toolboxForegroundColour: 'var(--mat-sys-on-surface)',
			flyoutBackgroundColour: 'var(--mat-sys-surface-container-highest)', flyoutForegroundColour: 'var(--mat-sys-on-surface-variant)', flyoutOpacity: .96,
			scrollbarColour: 'var(--mat-sys-outline)', scrollbarOpacity: .5
		},
		fontStyle: { family: 'Roboto, "Helvetica Neue", sans-serif', size: 11 }
	} );
}

//the code of a workspace, as the node's `code` holds it
function generate( workspace:Blockly.Workspace ):string{ return javascriptGenerator.workspaceToCode( workspace ).trimEnd(); }
//The code of a saved workspace, with no page under it:  what the editor writes beside the blocks.  What is no workspace throws.
export function flowBlocksCode( saved:unknown ):string{
	setup();
	const workspace = new Blockly.Workspace();
	try{
		Blockly.serialization.workspaces.load( <any>saved, workspace );
		return generate( workspace );
	}
	finally{
		workspace.dispose();
	}
}

//A workspace on the page, in `host`:  with the toolbox, or to be read alone.
export class FlowBlocksView{
	constructor( host:HTMLElement, saved:unknown, readonly:boolean ){
		const theme = setup();
		this.#workspace = Blockly.inject( host, {
			toolbox: readonly ? undefined : <any>flowToolbox, readOnly: readonly, theme, renderer: 'thrasos', sounds: false, trashcan: !readonly,
			media: new URL( 'assets/jde-opc/blockly/', document.baseURI ).href,
			move: {scrollbars: true, drag: true, wheel: true}, zoom: {controls: true, pinch: true, startScale: .9, minScale: .3, maxScale: 2},
			grid: {spacing: 24, length: 2, colour: 'var(--mat-sys-outline-variant)', snap: true}
		} );
		try{
			Blockly.serialization.workspaces.load( <any>saved, this.#workspace, {recordUndo: false} );
		}
		catch( e ){
			this.dispose();
			throw e;
		}
	}
	//`f` with the code after each change that is one of the blocks', not of the view or a drag under way
	onChange( f:( code:string )=>void ):void{
		this.#workspace.addChangeListener( e=>{
			if( !e.isUiEvent && !this.#workspace.isDragging() )
				f( this.code() );
		} );
	}
	code():string{ return generate( this.#workspace ); }
	save():object{ return Blockly.serialization.workspaces.save( this.#workspace ); }
	resize():void{ Blockly.svgResize( this.#workspace ); }
	//The blocks of these ids lit, each with the values plugged into it, which a statement's call stands for, and the first
	//brought into view;  how many of the ids are blocks of the workspace.
	light( ids:string[] ):number{
		this.#workspace.highlightBlock( null );
		const lit = ids.map( id=>this.#workspace.getBlockById( id ) ).filter( b=>!!b );
		const each = ( b:Blockly.BlockSvg ):void=>{
			this.#workspace.highlightBlock( b.id, true );
			for( const input of b.inputList ){
				const plugged = input.connection?.type==Blockly.ConnectionType.INPUT_VALUE ? <Blockly.BlockSvg|null>input.connection.targetBlock() : null;
				if( plugged )
					each( plugged );
			}
		};
		lit.forEach( each );
		if( lit.length )
			this.#workspace.centerOnBlock( lit[0].id );
		return lit.length;
	}
	focus():void{ Blockly.getFocusManager().focusTree( this.#workspace ); }
	dispose():void{ this.#workspace.dispose(); }
	#workspace:Blockly.WorkspaceSvg;
}