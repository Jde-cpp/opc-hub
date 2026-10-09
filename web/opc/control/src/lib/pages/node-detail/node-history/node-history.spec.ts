if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component, input, output } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { Observable, of, Subscriber } from 'rxjs';
import { ConfirmDialog, SnackbarService } from 'jde-framework';
import { HistEditArgs, HistEditResult, HistPage, HistReadArgs, HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { SubscriptionResult } from '../../../services/gateway-service';
import { HistoryService } from '../../../services/history-service';
import { HistEditDialog } from './hist-edit-dialog';
import { HistTrend } from './hist-trend';
import { NodeHistory } from './node-history';

//The trend stands in for itself:  hist-trend.spec drives the real chart, and under jsdom, which lays nothing out, a chart's
//axis has no length, so a redraw after a page lands throws a RangeError from Highcharts' time formatting of the NaN tick -
//nothing of the tab's.
@Component({ selector: 'hist-trend', template: '' })
class TrendStub{ values = input<HistValue[]>(); series = input<Variable[]>(); stepped = input( true ); loading = input( false ); hasEarlier = input( true ); loadEarlier = output<void>(); }

const settle = ()=>new Promise( r=>setTimeout(r) );
//A History tab on one historized node.  Each read is held until `answer` gives it a page;  `subscribed` counts the live
//tail's open subscriptions, and `push` sends the latest a value.  An edit sent directly lands in `edits.sent` and gets
//`edits.answer`;  the edit dialog is a stub that closes at once with `dialogResult.answer`, and the confirmation with `confirmed`.
const open = ()=>{
	const reads:{ args:HistReadArgs; answer:( page:HistPage )=>void }[] = [];
	const subscribers = new Set<Subscriber<SubscriptionResult>>();
	const gateway = { subscribe: ()=>new Observable<SubscriptionResult>( s=>{ subscribers.add(s); return ()=>subscribers.delete(s); } ), updateErrorCodes: async ()=>{} };
	const edits:{ sent:HistEditArgs[]; answer:HistEditResult } = { sent: [], answer: {values: [], nodes: []} };
	const dialogResult:{ answer:HistEditResult|undefined } = { answer: undefined };
	const errors:string[] = [];
	const y = {
		confirmed: true,
		dialog: { open: ( component:unknown )=>({ afterClosed: ()=>of( component===ConfirmDialog ? y.confirmed : component===HistEditDialog ? dialogResult.answer : undefined ) }) }
	};
	TestBed.configureTestingModule({ providers: [
		{ provide: HistoryService, useValue: {
			read: ( _reader:unknown, _source:unknown, args:HistReadArgs )=>new Promise<HistPage>( answer=>reads.push({args, answer}) ),
			edit: async ( _editor:unknown, _source:unknown, args:HistEditArgs )=>{ edits.sent.push( args ); return edits.answer; }
		} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}, info: ()=>{}, error: ( m:string )=>errors.push( m )} },
		{ provide: MatDialog, useValue: y.dialog }
	]});
	TestBed.overrideComponent( NodeHistory, {remove: {imports: [HistTrend]}, add: {imports: [TrendStub]}} );
	const node = new Variable( <any>{ns:2, i:1, name: 'A', browse: {ns:2, name: 'A'}, historizing: true} );
	const fixture = TestBed.createComponent( NodeHistory );
	fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'opc'}}, route: {profileKey: 'p'}, nodes: []} );//the chips are `candidates`, not these
	fixture.componentRef.setInput( 'candidates', [node] );
	fixture.detectChanges();
	return {
		fixture, node, reads, edits, dialogResult, errors,
		tab: fixture.componentInstance,
		set confirmed( v:boolean ){ y.confirmed = v; },
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
	//Modifications is the same reverse read with `modified`, paged the same way, with no live tail:  an edit isn't published
	it( 'reads the modifications in Modifications mode, without a live tail, and the values again on the way back', async ()=>{
		const tab = open();
		await tab.answer();
		tab.tab.mode.set( 'modified' );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 2 );
		expect( tab.reads[1].args.modified ).toBe( true );
		expect( tab.reads[1].args.nodes.map( n=>n.key ) ).toEqual( [tab.node.key] );
		expect( tab.subscribed() ).toBe( 0 );
		expect( tab.fixture.nativeElement.querySelector( 'hist-trend' ) ).toBeNull();
		await tab.answer( {values: [{...at(tab.node, 5000), modification: {time: new Date(9000), type: 'Delete', user: 'me'}}]} );
		tab.fixture.detectChanges();
		expect( tab.fixture.nativeElement.querySelector( '.row-count' )?.textContent?.trim() ).toBe( '1 modification' );
		expect( tab.fixture.nativeElement.querySelector( 'mat-header-cell.mat-column-modType' ) ).not.toBeNull();
		tab.tab.mode.set( 'values' );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].args.modified ).toBeUndefined();
		expect( tab.subscribed() ).toBe( 1 );
		tab.fixture.destroy();
	} );
	//After an edit the tab reads again what it held, a fresh list as long as the pages loaded:  a merge would have kept the
	//record the edit changed, and a plain reload would have dropped the pages Load earlier brought.
	it( 'reads the pages held again after an edit, as a fresh list', async ()=>{
		const tab = open();
		await tab.answer( {values: [at(tab.node, 5000)], continuation: 'page2'} );
		tab.tab.loadEarlier();
		await tab.answer( {values: [at(tab.node, 4000)], continuation: 'page3'} );
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [1, 1] );
		tab.dialogResult.answer = { values: [{node: tab.node.nodeId, source: new Date(5000), status: 0}], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.openEdit( 'replace', {node: tab.node, time: new Date(5000), value: 2} );
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].args.limit ).toBe( 2*NodeHistory.pageSize );
		expect( tab.reads[2].args.continuation ).toBeUndefined();
		expect( tab.tab.values() ).toEqual( [] );//not merged into
		await tab.answer( {values: [{...at(tab.node, 4000)}, {...at(tab.node, 5000), value: 2}], continuation: 'page3'} );
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [1, 2] );
		expect( tab.tab.hasEarlier() ).toBe( true );
		expect( tab.subscribed() ).toBe( 1 );//the live tail stays
		tab.fixture.destroy();
	} );
	//A row's Delete is a range purge from the value's time to itself (spec *Pass-through*), after the house confirmation;  a
	//refusal is said by its status and nothing is read again
	it( 'deletes a value as a range from its time to itself, once confirmed', async ()=>{
		const tab = open();
		await tab.answer( {values: [at(tab.node, 5000)]} );
		tab.confirmed = false;
		await tab.tab.deleteValue( tab.tab.values()[0] );
		expect( tab.edits.sent ).toEqual( [] );
		tab.confirmed = true;
		tab.edits.answer = { values: [], nodes: [{node: tab.node.nodeId, status: 0x80B00000}] };//Bad_NoData
		await tab.tab.deleteValue( tab.tab.values()[0] );
		expect( tab.edits.sent.map( e=>({...e, nodes: (<{nodes:NodeId[]}>e).nodes.map( n=>n.toString() )}) ) ).toEqual( [{kind: 'purgeRange', nodes: [tab.node.nodeId.toString()], start: new Date(5000), end: new Date(5000)}] );
		expect( tab.errors ).toHaveLength( 1 );
		expect( tab.reads ).toHaveLength( 1 );
		tab.edits.answer = { values: [], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.deleteValue( tab.tab.values()[0] );
		expect( tab.reads ).toHaveLength( 2 );
		tab.fixture.destroy();
	} );
	//the Edit menu and the rows' actions need the server's HistoryWrite bit on the node
	it( 'offers the edits only for a node this user may write', async ()=>{
		const tab = open();
		await tab.answer( {values: [at(tab.node, 5000)]} );
		tab.fixture.detectChanges();
		expect( tab.tab.writable() ).toEqual( [tab.node] );//no level given:  unknown, not a denial
		expect( tab.fixture.nativeElement.querySelector( '[aria-haspopup="menu"]' ) ).not.toBeNull();
		expect( tab.fixture.nativeElement.querySelectorAll( '.row-action' ) ).toHaveLength( 2 );
		const readOnly = new Variable( <any>{ns:2, i:1, name: 'A', browse: {ns:2, name: 'A'}, historizing: true, userAccessLevel: 5} );//Read|HistoryRead
		tab.fixture.componentRef.setInput( 'candidates', [readOnly] );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.tab.writable() ).toEqual( [] );
		expect( tab.fixture.nativeElement.querySelector( '[aria-haspopup="menu"]' ) ).toBeNull();
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