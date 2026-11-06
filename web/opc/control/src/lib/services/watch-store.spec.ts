import { Injector } from '@angular/core';
import { IPROFILE_SERVICE, ProfileStore } from 'jde-spa';
import { MockProfileService } from 'jde-spa/testing';
import { NodeId } from '../model/node-id';
import { WatchItemJson, watchProfileKey } from '../model/watch';
import { WatchStore } from './watch-store';

const A = new NodeId( {ns: 2, i: 1} ), B = new NodeId( {ns: 2, i: 2} ), C = new NodeId( {ns: 2, i: 3} );
const item = ( node:NodeId, parameters={} ):WatchItemJson=>({opc: 'local', node: node.toJson(), parameters});
//a browser tab:  a ProfileStore of its own - its own cache - over the one profile the user has
const tab = ( service:MockProfileService )=>Injector.create( {providers: [
	{ provide: IPROFILE_SERVICE, useValue: service },
	{ provide: ProfileStore, useFactory: ()=>new ProfileStore(), deps: [] },
	{ provide: WatchStore, useFactory: ()=>new WatchStore(), deps: [] }
]} ).get( WatchStore );
const held = ( service:MockProfileService )=>(<{items:WatchItemJson[]}>JSON.parse( service.rows.get( watchProfileKey( 'g' ) )! )).items.map( i=>new NodeId( i.node ).uaString() );

//The list was saved whole from what the page read, through a cache of its own per tab:  a row added from another tab, or by a
//second add on the same cache miss, was dropped by the next save (419-watch #9).
describe( 'WatchStore', ()=>{
	it( 'keeps a row another tab added when this tab removes or edits one', async ()=>{
		const service = new MockProfileService();
		service.rows.set( watchProfileKey( 'g' ), JSON.stringify( {items: [item( A ), item( B )]} ) );
		const page = tab( service ), nodePage = tab( service );
		expect( (await page.load( 'g' )).length ).toBe( 2 );
		expect( await nodePage.add( 'g', 'local', C ) ).toBe( true );
		expect( (await page.load( 'g' )).length ).toBe( 3 );//read past this tab's cache
		await page.remove( 'g', 'local', A );
		await page.setParameters( 'g', 'local', B, {samplingInterval: 100} );
		expect( held( service ) ).toEqual( ['ns=2;i=2', 'ns=2;i=3'] );
		expect( (await page.load( 'g' )).map( i=>i.parameters ) ).toEqual( [{samplingInterval: 100}, {}] );
	} );

	it( 'keeps both of two adds made before either is written', async ()=>{
		const service = new MockProfileService();
		const store = tab( service );
		const added = await Promise.all( [store.add( 'g', 'local', A ), store.add( 'g', 'local', B ), store.add( 'g', 'local', A )] );
		expect( added ).toEqual( [true, true, false] );
		expect( held( service ) ).toEqual( ['ns=2;i=1', 'ns=2;i=2'] );
	} );

	it( 'appends only the rows the list has not got', async ()=>{
		const service = new MockProfileService();
		service.rows.set( watchProfileKey( 'g' ), JSON.stringify( {items: [item( A )]} ) );
		expect( await tab( service ).append( 'g', [item( A ), item( B ), item( B )] ) ).toBe( 1 );
		expect( held( service ) ).toEqual( ['ns=2;i=1', 'ns=2;i=2'] );
	} );
} );