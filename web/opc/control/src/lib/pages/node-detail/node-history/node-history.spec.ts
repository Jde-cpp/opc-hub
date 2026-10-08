if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { Observable, Subscriber } from 'rxjs';
import { SnackbarService } from 'jde-framework';
import { HistPage, HistReadArgs, HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { SubscriptionResult } from '../../../services/gateway-service';
import { HistoryService } from '../../../services/history-service';
import { NodeHistory } from './node-history';

const settle = ()=>new Promise( r=>setTimeout(r) );
//A History tab on one historized node.  Each read is held until `answer` gives it a page;  `subscribed` counts the live
//tail's open subscriptions, and `push` sends the latest a value.
const open = ()=>{
	const reads:{ args:HistReadArgs; answer:( page:HistPage )=>void }[] = [];
	const subscribers = new Set<Subscriber<SubscriptionResult>>();
	const gateway = { subscribe: ()=>new Observable<SubscriptionResult>( s=>{ subscribers.add(s); return ()=>subscribers.delete(s); } ), updateErrorCodes: async ()=>{} };
	TestBed.configureTestingModule({ providers: [
		{ provide: HistoryService, useValue: {read: ( _reader:unknown, _source:unknown, args:HistReadArgs )=>new Promise<HistPage>( answer=>reads.push({args, answer}) )} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}} }
	]});
	const node = new Variable( <any>{ns:2, i:1, name: 'A', browse: {ns:2, name: 'A'}, historizing: true} );
	const fixture = TestBed.createComponent( NodeHistory );
	fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'opc'}}, route: {profileKey: 'p'}, nodes: []} );//the chips are `candidates`, not these
	fixture.componentRef.setInput( 'candidates', [node] );
	fixture.detectChanges();
	return {
		fixture, node, reads,
		tab: fixture.componentInstance,
		answer: async ( page:Partial<HistPage>={} )=>{ reads.at( -1 )!.answer( {values: [], continuation: null, nodes: [], ...page} ); await settle(); },
		subscribed: ()=>subscribers.size,
		push: ( source:Date )=>[...subscribers].at( -1 )!.next( {opcId: 'opc', node: node.nodeId, value: 1, sc: 0, source} ),
		drop: ()=>[...subscribers].at( -1 )!.error( {message: "Connection to the gateway was lost."} )//as Gateway.handleConnectionError does
	};
};
const at = ( node:Variable, ms:number ):HistValue=>({ node: node.nodeId, source: new Date(ms), server: null, status: 0, value: 1, bound: false, heartbeat: false });

describe( 'NodeHistory', ()=>{
	//The tail subscribed once the read returned, and the read ended at the browser's now:  a value between the two was in
	//neither (historian-web-trend #8).
	it( 'subscribes the live tail before the opening read, which ends past now', async ()=>{
		const before = Date.now();
		const tab = open();
		expect( tab.subscribed() ).toBe( 1 );
		expect( tab.reads[0].args.nodes.map( n=>n.key ) ).toEqual( [tab.node.key] );//NodeDetail's list, the tab's gate (historian-web-trend #15)
		expect( tab.reads[0].args.end!.getTime() ).toBeGreaterThanOrEqual( before+NodeHistory.endSlack );
		await tab.answer();
		expect( tab.subscribed() ).toBe( 1 );
		tab.fixture.destroy();
	} );
	//A read still out when the tab closed subscribed afterwards, under the owner the next History tab shares, and nothing
	//unsubscribed it (historian-web-trend #2).
	it( 'holds no subscription once destroyed', async ()=>{
		const tab = open();
		tab.fixture.destroy();
		await tab.answer();
		expect( tab.subscribed() ).toBe( 0 );
	} );
	//Load earlier read back from the oldest value held, and a node's first push is stamped with its last change, which can be
	//long before the page:  the read skipped all between (historian-web-trend #3).  It pages the opening read instead.
	it( 'loads earlier with the continuation, whatever the live tail holds', async ()=>{
		const tab = open();
		const opening = tab.reads[0].args;
		await tab.answer( {values: [at(tab.node, 5000)], continuation: 'page2'} );
		tab.push( new Date(1000) );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [1000, 5000] );
		tab.tab.loadEarlier();
		expect( tab.reads.length ).toBe( 2 );
		expect( tab.reads[1].args ).toEqual( {...opening, continuation: 'page2'} );

		await tab.answer( {values: [at(tab.node, 4000)]} );//the last page
		expect( tab.tab.hasEarlier() ).toBe( false );
		tab.tab.loadEarlier();
		expect( tab.reads.length ).toBe( 2 );
		tab.fixture.destroy();
	} );
	//A dropped connection errors the tail, and nothing subscribes again:  Live stayed on with nothing arriving
	//(historian-web-trend #12).  It turns off, and turning it on subscribes again.
	it( 'turns Live off when the connection drops', async ()=>{
		const tab = open();
		await tab.answer();
		tab.drop();
		tab.fixture.detectChanges();
		expect( tab.tab.live() ).toBe( false );
		expect( tab.subscribed() ).toBe( 0 );
		expect( tab.fixture.nativeElement.querySelector('mat-slide-toggle [role="switch"]')?.getAttribute('aria-checked') ).toBe( 'false' );
		tab.tab.onLiveChange( true );
		expect( tab.subscribed() ).toBe( 1 );
		tab.fixture.destroy();
	} );
	//Unticking every node while the read was out left loading on:  the stale read skipped clearing it (historian-web-trend #7).
	it( 'stops loading when the last node is unticked during a read', async ()=>{
		const tab = open();
		expect( tab.tab.loading() ).toBe( true );
		tab.tab.toggle( tab.node );
		tab.fixture.detectChanges();
		await tab.answer();
		tab.fixture.detectChanges();
		expect( tab.tab.loading() ).toBe( false );
		expect( tab.fixture.nativeElement.querySelector('.jde-empty .title')?.textContent ).toBe( 'No node selected.' );
		tab.fixture.destroy();
	} );
} );