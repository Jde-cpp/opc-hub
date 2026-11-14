import { TestBed } from '@angular/core/testing';
import { stable } from 'jde-spa/testing';
import { FlowBlockEditor } from './flow-block-editor';
import { FlowBlocksResult } from './flow-blocks-loader';

const saved = { blocks: { languageVersion: 0, blocks: [{
	type: 'controls_if', id: 'if', x: 24, y: 24, extraState: {hasElse: true},
	inputs: {
		IF0: {block: {type: 'logic_boolean', id: 'yes', fields: {BOOL: 'TRUE'}}},
		DO0: {block: {type: 'flow_msg_set', id: 'high', fields: {PATH: 'topic'}, inputs: {VALUE: {block: {type: 'text', id: 't', fields: {TEXT: 'high'}}}}}},
		ELSE: {block: {type: 'flow_msg_set', id: 'low', fields: {PATH: 'topic'}, inputs: {VALUE: {block: {type: 'text', id: 'u', fields: {TEXT: 'low'}}}}}}
	},
	next: {block: {type: 'flow_return', id: 'ret', inputs: {VALUE: {block: {type: 'flow_msg', id: 'm'}}}}}
}] } };
const create = async ( inputs:Record<string,unknown>={} )=>{
	const fixture = TestBed.createComponent( FlowBlockEditor );
	for( const [name, value] of Object.entries( {title: 'the mean', ...inputs} ) )
		fixture.componentRef.setInput( name, value );
	const done:FlowBlocksResult[] = [], closed:void[] = [];
	fixture.componentInstance.done.subscribe( y=>done.push( y ) );
	fixture.componentInstance.closed.subscribe( y=>closed.push( y ) );
	await stable( fixture );
	const element = <HTMLElement>fixture.nativeElement;
	const press = ( label:string )=>[...element.querySelectorAll( 'button' )].find( b=>b.textContent!.trim().endsWith( label ) )!.click();
	return { fixture, editor: fixture.componentInstance, element, done, closed, press };
};

//A Function node's body as blocks (#458):  the workspace on the page, and the code beside it.
describe( 'FlowBlockEditor', ()=>{
	beforeEach( ()=>{ vi.spyOn( HTMLCanvasElement.prototype, 'getContext' ).mockReturnValue( null ); } );//jsdom has none, and says so each time Blockly measures a text
	afterEach( ()=>vi.restoreAllMocks() );

	it( 'opens a saved workspace with its code beside it, and hands both back at Done', async ()=>{
		const { editor, element, done, closed, press } = await create( {blocks: saved} );
		expect( editor.fault() ).toBeUndefined();
		expect( element.querySelector( '.blocks .injectionDiv' ) ).toBeTruthy();
		expect( element.querySelector( '.blocklyToolbox' ) ).toBeTruthy();
		expect( element.querySelector( 'pre' )!.textContent ).toContain( "__block('high');\n  msg.topic = 'high';" );
		press( 'Done' );
		expect( done.length ).toBe( 1 );
		expect( done[0].code ).toBe( element.querySelector( 'pre' )!.textContent );
		expect( done[0].blocks ).toMatchObject( {blocks: {blocks: [{type: 'controls_if', id: 'if'}]}} );
		press( 'Cancel' );
		expect( closed.length ).toBe( 1 );
	} );
	it( 'starts a new body as the message returned', async ()=>{
		const { element } = await create();
		expect( element.querySelector( 'pre' )!.textContent ).toMatch( /^__block\('[^']{20}'\);\nreturn msg;$/ );
	} );
	it( 'shows a workspace to be read, with the blocks a message passed lit', async ()=>{
		const { editor, element, press, closed } = await create( {blocks: saved, code: 'the code deployed', readonly: true, lit: ['if', 'high', 'ret', 'gone']} );
		expect( element.querySelector( '.blocklyToolbox' ) ).toBeNull();
		expect( element.querySelector( '.blocks' )!.classList.contains( 'lit' ) ).toBe( true );
		expect( editor.passed() ).toBe( 3 );
		expect( element.querySelector( '.hint' )!.textContent ).toBe( 'The message passed 4 blocks, 1 of them not in this workspace.' );
		expect( element.querySelector( 'pre' )!.textContent ).toBe( 'the code deployed' );
		//the statements passed and the values plugged into them, and no block of the arm not taken
		expect( [...element.querySelectorAll( '.blocklyHighlighted' )].map( b=>b.getAttribute( 'data-id' ) ).sort() ).toEqual( ['high', 'if', 'm', 'ret', 't', 'yes'] );
		expect( [...element.querySelectorAll( 'button' )].map( b=>b.textContent!.trim() ) ).toEqual( ['closeClose'] );
		press( 'Close' );
		expect( closed.length ).toBe( 1 );
	} );
	it( 'says what does not read, and has nothing for Done', async ()=>{
		const { editor, element, done, press } = await create( {blocks: {blocks: {languageVersion: 0, blocks: [{type: 'flow_nothing'}]}}} );
		expect( editor.fault() ).toBeTruthy();
		expect( element.querySelector( '.fault' )!.textContent ).toContain( 'The blocks do not read' );
		press( 'Done' );
		expect( done ).toEqual( [] );
	} );
} );