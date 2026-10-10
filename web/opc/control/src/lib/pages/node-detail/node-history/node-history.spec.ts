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
import { By } from '@angular/platform-browser';
import { MatDialog } from '@angular/material/dialog';
import { Observable, of, Subscriber } from 'rxjs';
import { ConfirmData, ConfirmDialog, SnackbarService } from 'jde-framework';
import { alignUp, HistAggregate, HistEditArgs, HistEditResult, HistPage, HistReadArgs, HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { SubscriptionResult } from '../../../services/gateway-service';
import { HistoryService } from '../../../services/history-service';
import { HistEditDialog, HistEditDialogData, HistEditPreset } from './hist-edit-dialog';
import { HistTable } from './hist-table';
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
	const presets:(HistEditPreset|undefined)[] = [], confirmations:string[] = [];
	const errors:string[] = [], warnings:string[] = [];
	const y = {
		confirmed: true,
		aggregates: <HistAggregate[]>[{name: 'Average', browse: 'Average'}, {name: 'Median', browse: 'Median'}],
		dialog: { open: ( component:unknown, config?:{data?:HistEditDialogData|ConfirmData} )=>{
			if( component===HistEditDialog )
				presets.push( (<HistEditDialogData|undefined>config?.data)?.preset );
			else if( component===ConfirmDialog )
				confirmations.push( (<ConfirmData>config!.data).message );
			return { afterClosed: ()=>of( component===ConfirmDialog ? y.confirmed : component===HistEditDialog ? dialogResult.answer : undefined ) };
		} }
	};
	TestBed.configureTestingModule({ providers: [
		{ provide: HistoryService, useValue: {
			read: ( _reader:unknown, _source:unknown, args:HistReadArgs )=>new Promise<HistPage>( answer=>reads.push({args, answer}) ),
			edit: async ( _editor:unknown, _source:unknown, args:HistEditArgs )=>{ edits.sent.push( args ); return edits.answer; },
			aggregates: async ()=>{ await settle(); if( y.aggregates instanceof Error ) throw y.aggregates; return y.aggregates; }
		} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}, info: ()=>{}, error: ( m:string )=>errors.push( m ), warn: ( m:string )=>warnings.push( m )} },
		{ provide: MatDialog, useValue: y.dialog }
	]});
	TestBed.overrideComponent( NodeHistory, {remove: {imports: [HistTrend]}, add: {imports: [TrendStub]}} );
	const node = new Variable( <any>{ns:2, i:1, name: 'A', browse: {ns:2, name: 'A'}, historizing: true} );
	const fixture = TestBed.createComponent( NodeHistory );
	fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'opc'}}, route: {profileKey: 'p'}, nodes: []} );//the chips are `candidates`, not these
	fixture.componentRef.setInput( 'candidates', [node] );
	fixture.detectChanges();
	return {
		fixture, node, gateway, reads, edits, dialogResult, presets, confirmations, errors, warnings,
		tab: fixture.componentInstance,
		set confirmed( v:boolean ){ y.confirmed = v; },
		set aggregates( v:HistAggregate[]|Error ){ y.aggregates = <HistAggregate[]>v; },
		aggregate: async ( aggregate:string, interval:number )=>{ fixture.componentInstance.aggregation.set( {aggregate, interval} ); fixture.detectChanges(); await settle(); },
		answer: async ( page:Partial<HistPage>={} )=>{ reads.at( -1 )!.answer( {values: [], continuation: null, nodes: [], ...page} ); await settle(); },
		subscribed: ()=>subscribers.size,
		push: ( source:Date )=>[...subscribers].at( -1 )!.next( {opcId: 'opc', node: node.nodeId, value: 1, sc: 0, source} ),
		drop: ()=>[...subscribers].at( -1 )!.error( {message: "Connection to the gateway was lost."} )//as Gateway.handleConnectionError does
	};
};
const at = ( node:Variable, ms:number, nanos=ms%1000*1_000_000 ):HistValue=>({ node: node.nodeId, source: new Date(ms), server: null, sourceTime: {seconds: Math.floor(ms/1000), nanos}, status: 0, value: 1, bound: false, heartbeat: false });

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
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [1, 1] );//held until the page lands
		await tab.answer( {values: [{...at(tab.node, 4000)}, {...at(tab.node, 5000), value: 2}], continuation: 'page3'} );
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [1, 2] );
		expect( tab.tab.hasEarlier() ).toBe( true );
		expect( tab.subscribed() ).toBe( 1 );//the live tail stays
		tab.fixture.destroy();
	} );
	//A refresh emptied the values while it read:  the table went back to its first page and the trend blank.  What is held stays
	//until the page lands, and a push in the meantime outlives it (historian-web-edits #12).
	it( 'keeps the table on its page while an edit reads again', async ()=>{
		const tab = open();
		const opening = Array.from( {length: 250}, (_, i)=>at(tab.node, 1000*(i+1)) );
		await tab.answer( {values: opening} );
		tab.fixture.detectChanges();
		const table = <HistTable>tab.fixture.debugElement.query( By.directive(HistTable) ).componentInstance;
		table.pageIndex.set( 2 );
		tab.dialogResult.answer = { values: [{node: tab.node.nodeId, source: new Date(5000), status: 0}], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.openEdit( 'replace', {node: tab.node, time: new Date(5000), value: 2} );
		tab.fixture.detectChanges();
		expect( tab.reads ).toHaveLength( 2 );
		expect( tab.tab.values() ).toHaveLength( 250 );
		expect( table.pageIndex() ).toBe( 2 );
		tab.push( new Date(300_000) );
		await tab.answer( {values: opening.map( v=>v.source!.getTime()==5000 ? {...v, value: 2} : v )} );
		tab.fixture.detectChanges();
		expect( table.pageIndex() ).toBe( 2 );
		expect( tab.tab.values() ).toHaveLength( 251 );
		expect( tab.tab.values().find( v=>v.source!.getTime()==5000 )?.value ).toBe( 2 );
		expect( tab.tab.values().at( -1 )?.source?.getTime() ).toBe( 300_000 );
		tab.fixture.destroy();
	} );
	//A refresh read its pages in one read, counted it as one page, and Load earlier carried its limit:  3 pages held and an edit,
	//then Load earlier read 3000 more, and a second edit read 2000 of the 6000 held (historian-web-edits #3).
	it( 'counts the pages a refresh read, and loads earlier a page at a time after it', async ()=>{
		const tab = open();
		await tab.answer( {values: [at(tab.node, 5000)], continuation: 'page2'} );
		tab.tab.loadEarlier();
		await tab.answer( {values: [at(tab.node, 4000)], continuation: 'page3'} );
		tab.tab.loadEarlier();
		await tab.answer( {values: [at(tab.node, 3000)], continuation: 'page4'} );
		tab.dialogResult.answer = { values: [{node: tab.node.nodeId, source: new Date(5000), status: 0}], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.openEdit( 'replace', {node: tab.node, time: new Date(5000), value: 2} );
		expect( tab.reads.at( -1 )!.args.limit ).toBe( 3*NodeHistory.pageSize );
		await tab.answer( {values: [at(tab.node, 3000), at(tab.node, 4000), at(tab.node, 5000)], continuation: 'page4'} );
		tab.tab.loadEarlier();
		expect( tab.reads.at( -1 )!.args ).toMatchObject( {limit: NodeHistory.pageSize, continuation: 'page4'} );
		await tab.answer( {values: [at(tab.node, 2000)], continuation: 'page5'} );
		await tab.tab.openEdit( 'replace', {node: tab.node, time: new Date(5000), value: 3} );
		expect( tab.reads.at( -1 )!.args.limit ).toBe( 4*NodeHistory.pageSize );
		tab.fixture.destroy();
	} );
	//An edit the server took part of changed the history:  the tab reads it again, and names what was refused once the names
	//arrive (historian-web-edits #2)
	it( 'reads again after an edit the server took part of, and names what it refused', async ()=>{
		const tab = open();
		await tab.answer( {values: [at(tab.node, 5000)]} );
		tab.gateway.updateErrorCodes = async ()=>{ await settle(); OpcError.setMessages( [{sc: 0x809B0000, message: 'BadNoData'}] ); };//a round trip
		const other = new NodeId( {ns:2, i:9} );
		tab.dialogResult.answer = { values: [], nodes: [{node: tab.node.nodeId, status: 0}, {node: other, status: 0x809B0000}] };
		await tab.tab.openEdit( 'purgeRange' );
		expect( tab.reads ).toHaveLength( 2 );
		expect( tab.warnings ).toEqual( [`Deleted.  Refused for ${other.toString()}: BadNoData.`] );
		tab.fixture.destroy();
	} );
	//A row's Delete is a range purge from the value's time to itself (spec *Pass-through*), after the house confirmation;  a
	//refusal is said by its status and nothing is read again.  The time is the record's to the tick:  the Date's, cut to the
	//millisecond, named no record (historian-web-edits #1).  The status by name:  the text was built before the name came
	//back, so it read `Bad 0x801F0000` (historian-web-edits #11).  A code no other test here names.  The confirmation says whose
	//rule keeping the value is:  open62541's backend keeps no modification (historian-web-edits #13).
	it( 'deletes a value as a range from its time to itself, once confirmed', async ()=>{
		const tab = open();
		await tab.answer( {values: [at(tab.node, 5000, 123400)]} );
		tab.confirmed = false;
		await tab.tab.deleteValue( tab.tab.values()[0] );
		expect( tab.edits.sent ).toEqual( [] );
		expect( tab.confirmations[0] ).toContain( 'The bundled OPC server keeps the deleted value as a modification.  Another server may not.' );
		tab.confirmed = true;
		tab.edits.answer = { values: [], nodes: [{node: tab.node.nodeId, status: 0x801F0000}] };//Bad_UserAccessDenied
		tab.gateway.updateErrorCodes = async ()=>{ await settle(); OpcError.setMessages( [{sc: 0x801F0000, message: 'BadUserAccessDenied'}] ); };//a round trip
		await tab.tab.deleteValue( tab.tab.values()[0] );
		expect( tab.edits.sent.map( e=>({...e, nodes: (<{nodes:NodeId[]}>e).nodes.map( n=>n.toString() )}) ) ).toEqual( [{kind: 'purgeRange', nodes: [tab.node.nodeId.toString()], start: {seconds: 5, nanos: 123400}, end: {seconds: 5, nanos: 123400}}] );
		expect( tab.errors ).toEqual( ['Could not delete the value:  the server answered BadUserAccessDenied.'] );
		expect( tab.reads ).toHaveLength( 1 );
		tab.edits.answer = { values: [], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.deleteValue( tab.tab.values()[0] );
		expect( tab.reads ).toHaveLength( 2 );
		tab.fixture.destroy();
	} );
	//a row's Replace opens on the record's time to the tick, which the dialog sends while its field shows the row's (historian-web-edits #1).
	//Not on the row's status:  a Bad one, left in the field, stored the corrected value Bad again (historian-web-edits #9).
	it( "replaces a value from its row with the record's time, and no status", async ()=>{
		const tab = open();
		await tab.answer( {values: [{...at(tab.node, 5000, 123400), status: 0x808C0000}]} );//Bad_SensorFailure
		tab.tab.replaceValue( tab.tab.values()[0] );
		await settle();
		expect( tab.presets[0]?.time ).toEqual( new Date(5000) );
		expect( tab.presets[0]?.sourceTime ).toEqual( {seconds: 5, nanos: 123400} );
		expect( 'status' in tab.presets[0]! ).toBe( false );
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
	//An array node got the dialog's scalar editors:  a String[] took the text as one String, and a Double[]'s Replace could
	//never send (historian-web-edits #7).
	it( 'offers no edits on an array node', async ()=>{
		const tab = open();
		const array = new Variable( <any>{ns:2, i:1, name: 'A', browse: {ns:2, name: 'A'}, historizing: true, valueRank: 1, value: [1, 2]} );
		tab.fixture.componentRef.setInput( 'candidates', [array] );
		tab.fixture.detectChanges();
		await tab.answer( {values: [at(tab.node, 5000)]} );
		tab.fixture.detectChanges();
		expect( array.historyWritable ).toBe( true );
		expect( tab.tab.writable() ).toEqual( [] );
		expect( tab.fixture.nativeElement.querySelector( '[aria-haspopup="menu"]' ) ).toBeNull();
		expect( tab.fixture.nativeElement.querySelectorAll( '.row-action' ) ).toHaveLength( 0 );
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
	//The picker's list is the server's AggregateFunctions folder, read once;  a server without one lists none, and the raw
	//values stay to pick.  The picker is for the values:  a modification is a record.
	it( "lists the server's aggregates for the picker, and none from a server that refuses", async ()=>{
		const tab = open();
		await tab.answer();
		expect( tab.tab.aggregates().map( a=>a.browse ) ).toEqual( ['Average', 'Median'] );
		expect( tab.fixture.nativeElement.querySelector( 'hist-aggregate-picker' ) ).not.toBeNull();
		tab.tab.mode.set( 'modified' );
		tab.fixture.detectChanges();
		expect( tab.fixture.nativeElement.querySelector( 'hist-aggregate-picker' ) ).toBeNull();
		tab.aggregates = new Error( 'BadNodeIdUnknown' );//another connection, without the folder
		tab.fixture.componentRef.setInput( 'pageData', {gateway: tab.gateway, server: {connection: {slug: 'other'}}, route: {profileKey: 'p'}, nodes: []} );
		tab.fixture.detectChanges();
		await settle(); await settle();
		expect( tab.tab.aggregates() ).toEqual( [] );
		tab.fixture.destroy();
	} );
	//An aggregate read goes forward over both ends (a reverse one stamps each interval at its later edge), the last intervals a
	//page holds up to the boundary past now and none beyond, one value a node per interval;  a push is a reading, so there is no live tail, and
	//an aggregate is no record, so no row has a Replace or a Delete, while the Edit menu stays.
	it( 'reads an aggregate forward over the last intervals, on the clock, with no live tail and no row actions', async ()=>{
		const tab = open();
		await tab.answer();
		expect( tab.subscribed() ).toBe( 1 );
		const before = Date.now();
		await tab.aggregate( 'Average', 60_000 );
		expect( tab.reads ).toHaveLength( 2 );
		const args = tab.reads[1].args;
		expect( args ).toMatchObject( {aggregate: 'Average', interval: 60_000, limit: NodeHistory.pageSize} );
		expect( args.modified ).toBeUndefined();
		expect( args.continuation ).toBeUndefined();
		expect( args.end!.getTime() ).toBeGreaterThanOrEqual( before );//the interval holding now is the last:  no slack, which would be intervals of nothing
		expect( args.end!.getTime() ).toBeLessThanOrEqual( before+60_000+1_000 );
		expect( args.end!.getTime()%60_000 ).toBe( 0 );
		expect( args.start!.getTime() ).toBe( args.end!.getTime()-NodeHistory.pageSize*60_000 );
		expect( tab.subscribed() ).toBe( 0 );
		expect( tab.fixture.nativeElement.querySelector( 'mat-slide-toggle button' )?.hasAttribute( 'disabled' ) ).toBe( true );
		await tab.answer( {values: [{...at(tab.node, args.start!.getTime()), status: 0x00000401}]} );
		tab.fixture.detectChanges();
		expect( tab.tab.writable() ).toEqual( [tab.node] );
		expect( tab.fixture.nativeElement.querySelector( '[aria-haspopup="menu"]' ) ).not.toBeNull();
		expect( tab.fixture.nativeElement.querySelectorAll( '.row-action' ) ).toHaveLength( 0 );
		expect( tab.fixture.nativeElement.querySelector( 'hist-trend' ) ).not.toBeNull();
		tab.tab.aggregation.set( undefined );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].args.aggregate ).toBeUndefined();
		expect( tab.subscribed() ).toBe( 1 );
		tab.fixture.destroy();
	} );
	//Modifications are records:  an aggregate picked stays picked, and read again, on the way back, but the modified read is raw
	it( 'reads the modifications raw with an aggregate picked, and the aggregate again on the way back', async ()=>{
		const tab = open();
		await tab.answer();
		await tab.aggregate( 'Average', 60_000 );
		await tab.answer();
		tab.tab.mode.set( 'modified' );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].args ).toMatchObject( {modified: true, limit: NodeHistory.pageSize} );
		expect( tab.reads[2].args.aggregate ).toBeUndefined();
		expect( tab.reads[2].args.start ).toBeUndefined();
		tab.tab.mode.set( 'values' );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 4 );
		expect( tab.reads[3].args ).toMatchObject( {aggregate: 'Average', interval: 60_000} );
		expect( tab.tab.aggregation() ).toEqual( {aggregate: 'Average', interval: 60_000} );
		expect( tab.subscribed() ).toBe( 0 );
		tab.fixture.destroy();
	} );
	//the intervals a load holds share the page among the nodes, so every node's value for every interval comes in one page
	it( 'shares the page among the nodes of an aggregate read', async ()=>{
		const tab = open();
		const b = new Variable( <any>{ns:2, i:2, name: 'B', browse: {ns:2, name: 'B'}, historizing: true} ), c = new Variable( <any>{ns:2, i:3, name: 'C', browse: {ns:2, name: 'C'}, historizing: true} );
		tab.fixture.componentRef.setInput( 'candidates', [tab.node, b, c] );
		tab.fixture.detectChanges();
		await tab.answer();
		await tab.aggregate( 'Count', 1000 );
		const args = tab.reads.at( -1 )!.args;
		expect( NodeHistory.intervals( 3 ) ).toBe( 333 );
		expect( args.limit ).toBe( 999 );
		expect( args.end!.getTime()-args.start!.getTime() ).toBe( 333_000 );
		tab.fixture.destroy();
	} );
	//Load earlier is the range before, as long as a load, until a range answers nothing but Bad_NoData - no record in any of
	//its intervals, where the history begins
	it( 'loads the range before an aggregate read, and stops at one with no data', async ()=>{
		const tab = open();
		await tab.answer();
		await tab.aggregate( 'Average', 60_000 );
		const first = tab.reads[1].args;
		await tab.answer( {values: [at(tab.node, first.start!.getTime())]} );
		expect( tab.tab.hasEarlier() ).toBe( true );
		tab.tab.loadEarlier();
		expect( tab.reads ).toHaveLength( 3 );
		const earlier = tab.reads[2].args;
		expect( earlier ).toMatchObject( {aggregate: 'Average', interval: 60_000, limit: NodeHistory.pageSize} );
		expect( earlier.end!.getTime() ).toBe( first.start!.getTime() );
		expect( earlier.start!.getTime() ).toBe( first.start!.getTime()-NodeHistory.pageSize*60_000 );
		expect( earlier.continuation ).toBeUndefined();
		await tab.answer( {values: [{...at(tab.node, earlier.start!.getTime()), status: 0x809B0000, value: undefined}, {...at(tab.node, earlier.start!.getTime()+60_000), status: 0x809B0404, value: undefined}]} );
		expect( tab.tab.values() ).toHaveLength( 3 );
		expect( tab.tab.hasEarlier() ).toBe( false );
		tab.tab.loadEarlier();
		expect( tab.reads ).toHaveLength( 3 );
		tab.fixture.destroy();
	} );
	//the gateway caps `limit` at its readLimit and pages the rest of the range:  the rest is read within the load, and Load earlier
	//is still the range before
	it( "follows a capped aggregate range's continuation within the load", async ()=>{
		const tab = open();
		await tab.answer();
		await tab.aggregate( 'Average', 60_000 );
		const first = tab.reads[1].args;
		await tab.answer( {values: [at(tab.node, first.start!.getTime())], continuation: 'rest'} );
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].args ).toMatchObject( {aggregate: 'Average', continuation: 'rest'} );
		expect( tab.reads[2].args.start!.getTime() ).toBe( first.start!.getTime() );
		expect( tab.tab.loading() ).toBe( true );
		await tab.answer( {values: [at(tab.node, first.start!.getTime()+60_000)]} );
		expect( tab.tab.loading() ).toBe( false );
		expect( tab.tab.values() ).toHaveLength( 2 );
		tab.tab.loadEarlier();
		expect( tab.reads[3].args.end!.getTime() ).toBe( first.start!.getTime() );
		expect( tab.reads[3].args.continuation ).toBeUndefined();
		tab.fixture.destroy();
	} );
	//after an edit the tab reads the aggregates again, over the whole range loaded, up to the boundary past now
	it( 'reads the aggregate range loaded again after an edit', async ()=>{
		const tab = open();
		await tab.answer();
		await tab.aggregate( 'Average', 60_000 );
		const first = tab.reads[1].args;
		await tab.answer( {values: [at(tab.node, first.start!.getTime())]} );
		tab.tab.loadEarlier();
		const earlier = tab.reads[2].args;
		await tab.answer( {values: [at(tab.node, earlier.start!.getTime())]} );
		tab.dialogResult.answer = { values: [{node: tab.node.nodeId, source: new Date(first.start!.getTime()), status: 0}], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.openEdit( 'insert' );
		expect( tab.reads ).toHaveLength( 4 );
		const again = tab.reads[3].args;
		expect( again.start!.getTime() ).toBe( earlier.start!.getTime() );
		expect( again.end!.getTime() ).toBe( alignUp( again.end!.getTime(), 60_000 ) );
		expect( again.end!.getTime() ).toBeGreaterThanOrEqual( first.end!.getTime() );
		expect( again.limit ).toBe( (again.end!.getTime()-again.start!.getTime())/60_000 );
		expect( again.continuation ).toBeUndefined();
		expect( tab.tab.values() ).toHaveLength( 2 );//held until the page lands
		await tab.answer( {values: [at(tab.node, earlier.start!.getTime()), {...at(tab.node, first.start!.getTime()), value: 2}]} );
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [1, 2] );
		expect( tab.tab.hasEarlier() ).toBe( true );
		tab.fixture.destroy();
	} );
} );