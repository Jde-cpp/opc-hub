import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { vi } from 'vitest';
import { ExNodeId } from '../../../model/ex-node-id';
import { NodeKey } from '../../../model/node-id';
import { OpcObject, UaNode, Variable } from '../../../model/node';
import { ETypes } from '../../../model/types';
import { OpcStore } from '../../../services/opc-store';
import { HistNodePickerData, HistNodePickerDialog } from './hist-node-picker-dialog';

const settle = ()=>new Promise( r=>setTimeout( r ) );
const Pumps = 'urn:jde:pumps';
//the server:  namespace 2 is the pumps', 3 is listed by no URI
const server = { connection: {name: 'Local'}, namespaces: [{index: 0, uri: 'http://opcfoundation.org/UA/'}, {index: 2, uri: Pumps}], uri( ns:number ){ return this.namespaces.find( n=>n.index==ns )?.uri; } };
//Objects/pump1/{motorRpm, odd}:  odd is in the namespace with no URI
const pump1 = new OpcObject( <any>{ns: 2, i: 5001, name: 'pump1', browse: {ns: 2, name: 'pump1'}} );
const rpm = new Variable( <any>{ns: 2, i: 6012, name: 'rpm', browse: {ns: 2, name: 'motorRpm'}, dataType: {i: ETypes.Double}} );
const odd = new Variable( <any>{ns: 3, i: 1, name: 'odd', browse: {ns: 3, name: 'odd'}, dataType: {i: ETypes.Double}} );
const tree = new Map<NodeKey,UaNode[]>( [[OpcObject.rootNode.key, [pump1]], [pump1.key, [odd, rpm]]] );

const open = ( data:Partial<HistNodePickerData> )=>{
	const closed:unknown[] = [];
	const gateway = { browseObjectsFolder: vi.fn( async ( _c:string, parent:UaNode )=>tree.get( parent.key ) ?? [] ) };
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: { close: ( y:unknown )=>closed.push( y ) } },
		{ provide: OpcStore, useValue: { getConnection: async ()=>server } },
		{ provide: MAT_DIALOG_DATA, useValue: <HistNodePickerData>{ gateway: <any>gateway, title: 'Pick', mode: 'paths', connections: [{id: 1, slug: 'local', name: 'Local'}], ...data } }
	]});
	const fixture = TestBed.createComponent( HistNodePickerDialog );
	fixture.detectChanges();
	return { fixture, dialog: fixture.componentInstance, gateway, closed, settle: async ()=>{ await settle(); fixture.detectChanges(); await settle(); fixture.detectChanges(); } };
};

describe( 'HistNodePickerDialog', ()=>{
	//the one connection is taken as read;  a folder opens, a variable is picked with its path from the start by URI, and a variable
	//in a namespace the server lists by no URI can't be
	it( 'browses from the Objects folder and picks a variable with its path by URI', async ()=>{
		const d = open( {} );
		await d.settle();
		expect( d.dialog.connection() ).toBe( 'local' );
		expect( d.dialog.trail().length ).toBe( 1 );
		expect( d.dialog.children().map( n=>n.name ) ).toEqual( ['pump1'] );
		await d.dialog.open( pump1 );
		expect( d.dialog.trail().map( n=>n.name ) ).toEqual( ['root', 'pump1'] );
		expect( d.dialog.children().map( n=>n.name ) ).toEqual( ['odd', 'rpm'] );
		expect( d.dialog.unspelled( odd ) ).toBe( true );
		expect( d.dialog.unspelled( rpm ) ).toBe( false );
		d.dialog.toggle( rpm );
		expect( d.dialog.picks() ).toEqual( [{ node: new ExNodeId( {ns: 2, i: 6012, nsu: Pumps} ), path: [{nsu: Pumps, name: 'pump1'}, {nsu: Pumps, name: 'motorRpm'}], name: 'rpm' }] );
		d.dialog.toggle( rpm );
		expect( d.dialog.picks() ).toEqual( [] );
		d.dialog.toggle( rpm );
		d.dialog.done();
		expect( d.closed ).toHaveLength( 1 );
		expect( (<any[]>d.closed[0])[0].path ).toHaveLength( 2 );
		await d.dialog.up( 0 );
		expect( d.dialog.trail().length ).toBe( 1 );
		expect( d.dialog.children().map( n=>n.name ) ).toEqual( ['pump1'] );
	} );
	//a group's picker starts at the group's root, by URI through the server's namespaces, and a path leaves the root out
	it( 'starts from a root given, whose paths leave it out', async ()=>{
		const d = open( {mode: 'nodes', connection: 'local', root: new ExNodeId( {i: 5001, nsu: Pumps} )} );
		await d.settle();
		expect( d.gateway.browseObjectsFolder.mock.calls[0][1].ns ).toBe( 2 );
		expect( d.dialog.startName() ).toBe( '{"nsu":"urn:jde:pumps","i":5001}' );
		expect( d.dialog.children().map( n=>n.name ) ).toEqual( ['odd', 'rpm'] );
		d.dialog.toggle( rpm );
		expect( d.dialog.picks()[0].path ).toEqual( [{nsu: Pumps, name: 'motorRpm'}] );
	} );
	it( 'refuses a root in a namespace the server does not have', async ()=>{
		const d = open( {connection: 'local', root: new ExNodeId( {i: 1, nsu: 'urn:other'} )} );
		await d.settle();
		expect( d.dialog.error() ).toContain( "has no namespace 'urn:other'" );
		expect( d.dialog.children() ).toEqual( [] );
	} );
	//the object picker closes on the folder shown, or one in the list, by URI
	it( 'chooses an object', async ()=>{
		const d = open( {mode: 'object', connection: 'local'} );
		await d.settle();
		expect( d.dialog.displayedColumns ).toContain( 'choose' );
		await d.dialog.open( pump1 );
		d.dialog.chooseCurrent();
		expect( d.closed ).toEqual( [[{ node: new ExNodeId( {ns: 2, i: 5001, nsu: Pumps} ), path: [{nsu: Pumps, name: 'pump1'}], name: 'pump1' }]] );
	} );
	//a template's picker lets the folder shown become the root:  the root text is its node by URI, and the trail starts over there
	it( 'makes the folder shown the root', async ()=>{
		const d = open( {rootEditable: true} );
		await d.settle();
		await d.dialog.open( pump1 );
		await d.dialog.useAsRoot();
		expect( d.dialog.rootText() ).toBe( '{"nsu":"urn:jde:pumps","i":5001}' );
		expect( d.dialog.trail().length ).toBe( 1 );
		expect( d.dialog.children().map( n=>n.name ) ).toEqual( ['odd', 'rpm'] );
		d.dialog.toggle( rpm );
		expect( d.dialog.picks()[0].path ).toEqual( [{nsu: Pumps, name: 'motorRpm'}] );
		d.dialog.rootText.set( 'nonsense' );
		expect( d.dialog.rootError() ).toBeDefined();
	} );
} );