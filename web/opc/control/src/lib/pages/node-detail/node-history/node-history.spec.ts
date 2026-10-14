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
import { vi } from 'vitest';
import { ConfirmData, ConfirmDialog, SnackbarService } from 'jde-framework';
import { alignUp, HistAggregate, HistCapabilities, HistEditArgs, HistEditResult, HistGroupMembers, HistPage, HistReadArgs, HistSource, HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { HistEvent, HistSubscribeArgs, SubscriptionResult } from '../../../services/gateway-service';
import { HistoryService } from '../../../services/history-service';
import { HistEditDialog, HistEditDialogData, HistEditPreset } from './hist-edit-dialog';
import { HistThresholdsDialog, HistThresholdsDialogData } from './hist-thresholds-dialog';
import { HistTable } from './hist-table';
import { HistTrend } from './hist-trend';
import { NodeHistory } from './node-history';

//The trend stands in for itself:  hist-trend.spec drives the real chart, and under jsdom, which lays nothing out, a chart's
//axis has no length, so a redraw after a page lands throws a RangeError from Highcharts' time formatting of the NaN tick -
//nothing of the tab's.
@Component({ selector: 'hist-trend', template: '' })
class TrendStub{ values = input<HistValue[]>(); series = input<Variable[]>(); stepped = input( true ); loading = input( false ); hasEarlier = input( true ); loadEarlier = output<void>(); }

const settle = ()=>new Promise( r=>setTimeout(r) );
const A = new Variable( <any>{ns:2, i:1, name: 'A', browse: {ns:2, name: 'A'}, historizing: true} );
//A History tab on one historized node, A, or on the `candidates` and `groups` given.  Each read is held until `answer` gives it a
//page, and keeps the source it named;  `subscribed` counts the live tail's open subscriptions, and `push` sends the latest a value.
//A group's subscription is `histSubscribed`, its arguments `histArgs`, and `emit` sends the latest an event, `histError` an end.
//An edit sent directly lands in `edits.sent`, its source in `edits.sources`, and gets `edits.answer`;  the edit dialog is a stub
//that closes at once with `dialogResult.answer`, and the confirmation with `confirmed`.  A group's capabilities are `capabilities`.
const open = ( options:{candidates?:Variable[]; groups?:HistGroupMembers[]}={} )=>{
	const reads:{ source:HistSource; args:HistReadArgs; answer:( page:HistPage )=>void }[] = [];
	const subscribers = new Set<Subscriber<SubscriptionResult>>();
	const histSubscribers:{ args:HistSubscribeArgs; subscriber:Subscriber<HistEvent> }[] = [];
	const gateway = {
		subscribe: ()=>new Observable<SubscriptionResult>( s=>{ subscribers.add(s); return ()=>subscribers.delete(s); } ),
		histSubscribe: ( args:HistSubscribeArgs )=>new Observable<HistEvent>( s=>{ const entry = {args, subscriber: s}; histSubscribers.push( entry ); return ()=>{ histSubscribers.splice( histSubscribers.indexOf(entry), 1 ); }; } ),
		updateErrorCodes: async ()=>{}, slug: 'gw'
	};
	const edits:{ sent:HistEditArgs[]; sources:HistSource[]; answer:HistEditResult } = { sent: [], sources: [], answer: {values: [], nodes: []} };
	const dialogResult:{ answer:HistEditResult|undefined } = { answer: undefined };
	const presets:(HistEditPreset|undefined)[] = [], confirmations:string[] = [], thresholds:HistThresholdsDialogData[] = [];
	const errors:string[] = [], warnings:string[] = [];
	const thresholdReads:{reader:unknown; group:number}[] = [], overrides:{poster:unknown; id:number; changes:unknown}[] = [];
	const y = {
		confirmed: true,
		aggregates: <HistAggregate[]>[{name: 'Average', browse: 'Average'}, {name: 'Median', browse: 'Median'}],
		capabilities: <HistCapabilities>{aggregates: [{name: 'Median', browse: 'Median'}], update: false, delete: false},
		dialog: { open: ( component:unknown, config?:{data?:HistEditDialogData|ConfirmData|HistThresholdsDialogData} )=>{
			if( component===HistEditDialog )
				presets.push( (<HistEditDialogData|undefined>config?.data)?.preset );
			else if( component===ConfirmDialog )
				confirmations.push( (<ConfirmData>config!.data).message );
			else if( component===HistThresholdsDialog )
				thresholds.push( <HistThresholdsDialogData>config!.data );
			return { afterClosed: ()=>of( component===ConfirmDialog ? y.confirmed : component===HistEditDialog ? dialogResult.answer : undefined ) };
		} }
	};
	TestBed.configureTestingModule({ providers: [
		{ provide: HistoryService, useValue: {
			read: ( _reader:unknown, source:HistSource, args:HistReadArgs )=>new Promise<HistPage>( answer=>reads.push({source, args, answer}) ),
			edit: async ( _editor:unknown, source:HistSource, args:HistEditArgs )=>{ edits.sent.push( args ); edits.sources.push( source ); return edits.answer; },
			aggregates: async ()=>{ await settle(); if( y.aggregates instanceof Error ) throw y.aggregates; return y.aggregates; },
			capabilities: async ()=>{ await settle(); if( y.capabilities instanceof Error ) throw y.capabilities; return y.capabilities; },
			thresholds: async ( reader:unknown, group:number )=>{ thresholdReads.push( {reader, group} ); return {update: false, nodes: []}; },
			override: async ( poster:unknown, id:number, changes:unknown )=>{ overrides.push( {poster, id, changes} ); }
		} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}, info: ()=>{}, error: ( m:string )=>errors.push( m ), warn: ( m:string )=>warnings.push( m )} },
		{ provide: MatDialog, useValue: y.dialog }
	]});
	TestBed.overrideComponent( NodeHistory, {remove: {imports: [HistTrend]}, add: {imports: [TrendStub]}} );
	const node = A;
	const fixture = TestBed.createComponent( NodeHistory );
	fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'opc'}}, route: {profileKey: 'p'}, nodes: []} );//the chips are `candidates`, not these
	fixture.componentRef.setInput( 'candidates', options.candidates ?? [node] );
	fixture.componentRef.setInput( 'groups', options.groups ?? [] );
	fixture.detectChanges();
	return {
		fixture, node, gateway, reads, edits, dialogResult, presets, confirmations, errors, warnings, thresholds, thresholdReads, overrides,
		tab: fixture.componentInstance,
		set confirmed( v:boolean ){ y.confirmed = v; },
		set aggregates( v:HistAggregate[]|Error ){ y.aggregates = <HistAggregate[]>v; },
		set capabilities( v:HistCapabilities|Error ){ y.capabilities = <HistCapabilities>v; },
		actions: ()=>[...fixture.nativeElement.querySelectorAll( '.row-action' )].map( ( b:Element )=>b.getAttribute( 'aria-label' ) ),
		aggregate: async ( aggregate:string, interval:number )=>{ fixture.componentInstance.aggregation.set( {aggregate, interval} ); fixture.detectChanges(); await settle(); },
		answer: async ( page:Partial<HistPage>={} )=>{ reads.at( -1 )!.answer( {values: [], continuation: null, nodes: [], ...page} ); await settle(); },
		subscribed: ()=>subscribers.size,
		push: ( source:Date )=>[...subscribers].at( -1 )!.next( {opcId: 'opc', node: node.nodeId, value: 1, sc: 0, source} ),
		drop: ()=>[...subscribers].at( -1 )!.error( {message: "Connection to the gateway was lost."} ),//as Gateway.handleConnectionError does
		histSubscribed: ()=>histSubscribers.length,
		histArgs: ()=>histSubscribers.at( -1 )!.args,
		emit: ( e:HistEvent )=>{ histSubscribers.at( -1 )!.subscriber.next( e ); fixture.detectChanges(); },
		histError: ( e:unknown )=>{ histSubscribers.at( -1 )!.subscriber.error( e ); fixture.detectChanges(); }
	};
};
const group = { id: 3, name: 'Pumps', nodes: [A] };
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
	//the live tail's pushes count for no page:  a refresh read the opening's page and dropped the pushes before it (historian-live-trend #9)
	it( 'reads again as many values as the live tail pushed after an edit', async ()=>{
		const tab = open();
		await tab.answer( {values: [at(tab.node, 1000)]} );
		for( let i=0; i<1200; ++i )
			tab.push( new Date(2000+1000*i) );
		tab.dialogResult.answer = { values: [{node: tab.node.nodeId, source: new Date(1000), status: 0}], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.openEdit( 'replace', {node: tab.node, time: new Date(1000), value: 2} );
		expect( tab.reads.at( -1 )!.args.limit ).toBe( 2*NodeHistory.pageSize );
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
	//A group's history (#335):  the read names the group, there is no live tail yet (#336), the picker lists the historian's
	//aggregates from the group's capabilities, and the edits are what the gateway granted - here nothing, hist_groups being
	//unenforced, whatever the node's own bits say.
	//A group's trend opens on HistSubscribe's snapshot (spec *Subscriptions*), the last snapshotWindow from each node's bound, not a
	//read, with loading on until the last page;  the ack's statuses stand where a read's node statuses do;  Load earlier is a
	//reverse read ending where the snapshot began.  Its aggregates are the group's, and no edits the gateway didn't grant.
	it( "opens a group on its snapshot, no read, with its aggregates, and no edits the gateway didn't grant", async ()=>{
		const before = Date.now();
		const tab = open( {candidates: [], groups: [group]} );
		expect( tab.tab.sources().map( s=>s.label ) ).toEqual( ['Pumps'] );
		expect( tab.fixture.nativeElement.querySelector( '.source-group' ) ).toBeNull();//one source:  nothing to pick
		expect( tab.reads ).toHaveLength( 0 );
		expect( tab.subscribed() ).toBe( 0 );
		expect( tab.histSubscribed() ).toBe( 1 );
		const args = tab.histArgs();
		expect( args.group ).toBe( 3 );
		expect( args.nodes.map( n=>n.key ) ).toEqual( [A.key] );
		expect( args.from!.getTime() ).toBeGreaterThanOrEqual( before-NodeHistory.snapshotWindow );
		expect( args.from!.getTime() ).toBeLessThanOrEqual( Date.now()-NodeHistory.snapshotWindow );
		expect( tab.tab.loading() ).toBe( true );
		expect( tab.fixture.nativeElement.querySelector( 'mat-slide-toggle button' )?.hasAttribute( 'disabled' ) ).toBe( false );
		tab.emit( {kind: 'ack', statuses: [0x80340000]} );//Bad_NodeIdUnknown
		expect( tab.fixture.nativeElement.querySelector( '.refused' )?.textContent ).toContain( 'the gateway answered' );
		tab.emit( {kind: 'snapshot', values: [{...at(A, args.from!.getTime()-1000), bound: true}], more: true} );
		expect( tab.tab.loading() ).toBe( true );
		tab.emit( {kind: 'snapshot', values: [at(A, args.from!.getTime()+1000)], more: false} );
		expect( tab.tab.loading() ).toBe( false );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [args.from!.getTime()-1000, args.from!.getTime()+1000] );
		await settle();
		tab.fixture.detectChanges();
		expect( tab.tab.aggregates().map( a=>a.browse ) ).toEqual( ['Median'] );
		expect( tab.tab.writable() ).toEqual( [] );
		expect( tab.fixture.nativeElement.querySelector( '[aria-haspopup="menu"]' ) ).toBeNull();
		expect( tab.actions() ).toEqual( [] );
		tab.tab.loadEarlier();
		expect( tab.reads ).toHaveLength( 1 );
		expect( tab.reads[0].source ).toEqual( {group: 3} );
		expect( tab.reads[0].args.nodes.map( n=>n.key ) ).toEqual( [A.key] );
		expect( tab.reads[0].args ).toMatchObject( {end: args.from, limit: NodeHistory.pageSize} );
		expect( tab.reads[0].args.start ).toBeUndefined();
		expect( tab.reads[0].args.continuation ).toBeUndefined();
		await tab.answer( {values: [at(A, args.from!.getTime()-5000)]} );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [args.from!.getTime()-5000, args.from!.getTime()-1000, args.from!.getTime()+1000] );
		expect( tab.tab.hasEarlier() ).toBe( false );
		tab.fixture.destroy();
		expect( tab.histSubscribed() ).toBe( 0 );
	} );
	//the stream's values merge in as the tail's pushes do, and a modification pushed redraws the point it targets:  the value put
	//there, or none for a delete
	it( "merges a group's stream, and applies a pushed modification to the record it targets", ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [at(A, 5000), at(A, 7000)], more: false} );
		tab.emit( {kind: 'records', values: [at(A, 6000), at(A, 8000)], modifications: []} );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [5000, 6000, 7000, 8000] );
		tab.emit( {kind: 'records', values: [], modifications: [{node: A.nodeId, target: new Date(6000), kind: 'replace', value: {...at(A, 6000), value: 9}, time: new Date(9000), user: 'me'}]} );
		expect( tab.tab.values()[1].value ).toBe( 9 );
		tab.emit( {kind: 'records', values: [], modifications: [{node: A.nodeId, target: new Date(7000), kind: 'delete', time: new Date(9001), user: 'me'}]} );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [5000, 6000, 8000] );
		expect( tab.fixture.nativeElement.querySelector( '.row-count' )?.textContent?.trim() ).toBe( '3 values' );
		tab.fixture.destroy();
	} );
	//A range purge pushes a delete per record it took, and each was a filter of all held and a write:  5000 held and purged were
	//25M visits on the UI thread (historian-live-trend #12).  A records event's modifications are one write.
	it( "applies a group's pushed purge in one write", ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: Array.from( {length: 5000}, (_, i)=>at(A, 1000*(i+1)) ), more: false} );
		const update = vi.spyOn( tab.tab.values, 'update' );
		try{
			tab.emit( {kind: 'records', values: [], modifications: Array.from( {length: 4999}, (_, i)=>({node: A.nodeId, target: new Date(1000*(i+1)), kind: 'delete' as const, time: new Date(9_000_000), user: 'me'}) )} );
			expect( update ).toHaveBeenCalledTimes( 1 );
			expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [5_000_000] );
		}
		finally{ update.mockRestore(); }
		tab.fixture.destroy();
	} );
	//the gateway ends a subscription with an exception under its id:  the group removed, or the historian stopped.  Live turns off
	//and the trend says why;  turning it on subscribes again from the latest value held
	it( "turns Live off and says why when the gateway ends a group's subscription, and resumes from the latest value held", ()=>{
		const t = Date.now()-60_000;
		const tab = open( {candidates: [], groups: [group]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [at(A, t), at(A, t+2000)], more: false} );
		tab.histError( {error: {requestId: 1, message: "The group was removed, which ends the subscription.", httpStatus: 404}} );
		expect( tab.tab.live() ).toBe( false );
		expect( tab.histSubscribed() ).toBe( 0 );
		expect( tab.fixture.nativeElement.querySelector( '.live-state' )?.textContent ).toContain( '(404)The group was removed' );
		expect( tab.tab.values() ).toHaveLength( 2 );
		tab.tab.onLiveChange( true );
		tab.fixture.detectChanges();
		expect( tab.histSubscribed() ).toBe( 1 );
		expect( tab.histArgs().from ).toEqual( new Date(t+2000) );
		expect( tab.fixture.nativeElement.querySelector( '.live-state' ) ).toBeNull();
		tab.fixture.destroy();
	} );
	//a dropped socket:  the gateway subscribes again when it is back, and the tab says so until the ack
	it( "says a group's subscription was dropped until the gateway's next ack", ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [at(A, 5000)], more: true} );
		tab.emit( {kind: 'dropped'} );
		expect( tab.tab.loading() ).toBe( false );
		expect( tab.tab.live() ).toBe( true );
		expect( tab.fixture.nativeElement.querySelector( '.live-state' )?.textContent ).toContain( 'Connection to the gateway lost' );
		tab.emit( {kind: 'ack', statuses: [0]} );
		expect( tab.fixture.nativeElement.querySelector( '.live-state' ) ).toBeNull();
		expect( tab.tab.loading() ).toBe( true );
		tab.emit( {kind: 'snapshot', values: [at(A, 5000), at(A, 6000)], more: false} );
		expect( tab.tab.loading() ).toBe( false );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [5000, 6000] );
		tab.fixture.destroy();
	} );
	//Live off mid-snapshot lets the snapshot's loading go, so Load earlier reads before the window (historian-live-trend #1)
	it( "stops loading when Live goes off before a group's snapshot is in", ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		const loadEarlier = ()=>[...tab.fixture.nativeElement.querySelectorAll( 'button' )].find( ( b:Element )=>b.textContent?.includes( 'Load earlier' ) )!;
		const from = tab.histArgs().from!;
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [at(A, from.getTime()+1000)], more: true} );
		expect( loadEarlier().disabled ).toBe( true );
		tab.tab.onLiveChange( false );
		tab.fixture.detectChanges();
		expect( tab.histSubscribed() ).toBe( 0 );
		expect( tab.tab.loading() ).toBe( false );
		expect( loadEarlier().disabled ).toBe( false );
		tab.tab.loadEarlier();
		expect( tab.reads[0].args ).toMatchObject( {end: from} );
		tab.fixture.destroy();
	} );
	//a read's loading is the read's:  Live off leaves it on until the page lands
	it( "leaves a Load earlier read loading when Live goes off on a group", async ()=>{
		const reader = open( {candidates: [], groups: [group]} );
		reader.emit( {kind: 'ack', statuses: [0]} );
		reader.emit( {kind: 'snapshot', values: [at(A, 5000)], more: false} );
		reader.tab.loadEarlier();
		expect( reader.tab.loading() ).toBe( true );
		reader.tab.onLiveChange( false );
		expect( reader.tab.loading() ).toBe( true );
		await reader.answer( {values: [at(A, 4000)]} );
		expect( reader.tab.loading() ).toBe( false );
		reader.fixture.destroy();
	} );
	//the lines saying a group's subscription ended or was dropped are that subscription's:  a change of source takes them away,
	//as Live off does (historian-live-trend #2)
	it( "drops a group's ended line with a change of source", async ()=>{
		const tab = open( {groups: [group]} );
		await tab.answer();
		tab.tab.pick( 'group/3' );
		tab.fixture.detectChanges();
		await settle();
		tab.histError( {error: {requestId: 1, message: "The group was removed, which ends the subscription.", httpStatus: 404}} );
		expect( tab.fixture.nativeElement.querySelector( '.live-state' )?.textContent ).toContain( '(404)The group was removed' );
		tab.tab.pick( 'opc/opc' );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads.at( -1 )!.source ).toEqual( {opc: 'opc'} );
		expect( tab.fixture.nativeElement.querySelector( '.live-state' ) ).toBeNull();
		tab.fixture.destroy();
	} );
	it( "drops a group's dropped line when Live goes off", ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'dropped'} );
		expect( tab.fixture.nativeElement.querySelector( '.live-state' )?.textContent ).toContain( 'Connection to the gateway lost' );
		tab.tab.onLiveChange( false );
		tab.fixture.detectChanges();
		expect( tab.fixture.nativeElement.querySelector( '.live-state' ) ).toBeNull();
		tab.fixture.destroy();
	} );
	//a dropped subscription pushes nothing:  an edit then reads again, as with Live off, until the ack (historian-live-trend #3)
	it( "reads again after an edit while a group's subscription is dropped", async ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.capabilities = {aggregates: [], update: true, delete: true};
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [at(A, 5000)], more: false} );
		await settle();
		tab.dialogResult.answer = { values: [{node: A.nodeId, source: new Date(5000), status: 0}], nodes: [] };
		await tab.tab.openEdit( 'replace', {node: A, time: new Date(5000), value: 2} );
		expect( tab.reads ).toHaveLength( 0 );//the stream pushes it
		tab.emit( {kind: 'dropped'} );
		await tab.tab.openEdit( 'replace', {node: A, time: new Date(5000), value: 3} );
		expect( tab.reads ).toHaveLength( 1 );
		expect( tab.reads[0].source ).toEqual( {group: 3} );
		await tab.answer( {values: [at(A, 5000)]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		await tab.tab.openEdit( 'replace', {node: A, time: new Date(5000), value: 4} );
		expect( tab.reads ).toHaveLength( 1 );
		tab.fixture.destroy();
	} );
	//a pushed insert before the earliest value held is Load earlier's to read, until it has nothing left:  then it goes in
	//(historian-live-trend #4)
	it( "puts a pushed insert before the earliest value once Load earlier has nothing left", async ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		const insert = ( ms:number ):HistEvent=>({kind: 'records', values: [], modifications: [{node: A.nodeId, target: new Date(ms), kind: 'insert', value: {...at(A, ms), value: 9}, time: new Date(9000), user: 'me'}]});
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [at(A, 5000)], more: false} );
		tab.emit( insert(3000) );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [5000] );
		tab.tab.loadEarlier();
		await tab.answer( {values: [at(A, 3000), at(A, 4000)]} );
		expect( tab.tab.hasEarlier() ).toBe( false );
		tab.emit( insert(2000) );
		expect( tab.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [2000, 3000, 4000, 5000] );
		expect( tab.tab.values()[0].value ).toBe( 9 );
		tab.fixture.destroy();
	} );
	//Live on again resumes from the oldest of the nodes' latest values held, not the newest of all:  a node whose clock runs behind
	//would get only its bound (historian-live-trend #5)
	it( "resumes a group from the oldest of its nodes' latest values when Live goes on again", ()=>{
		const b = new Variable( <any>{ns:2, i:2, name: 'B', browse: {ns:2, name: 'B'}, historizing: true} );
		const t = Date.now()-60_000;
		const tab = open( {candidates: [], groups: [{...group, nodes: [A, b]}]} );
		tab.emit( {kind: 'ack', statuses: [0, 0]} );
		tab.emit( {kind: 'snapshot', values: [at(b, t), at(A, t+1000), at(A, t+2000)], more: false} );
		tab.tab.onLiveChange( false );
		tab.tab.onLiveChange( true );
		expect( tab.histArgs().from ).toEqual( new Date(t) );
		tab.fixture.destroy();
	} );
	//The resume streamed all since the latest value held:  Live left off overnight, then on, streamed the night page by page
	//(historian-live-trend #13).  With none held inside the opening's window, the tab opens again, and Load earlier reads before it.
	it( "opens a group again when Live goes on with nothing held inside the opening's window", ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [at(A, 5000), at(A, 7000)], more: false} );
		tab.tab.onLiveChange( false );
		const before = Date.now();
		tab.tab.onLiveChange( true );
		expect( tab.histSubscribed() ).toBe( 1 );
		const from = tab.histArgs().from!;
		expect( from.getTime() ).toBeGreaterThanOrEqual( before-NodeHistory.snapshotWindow );
		expect( tab.tab.values() ).toEqual( [] );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [{...at(A, from.getTime()-1000), bound: true}], more: false} );
		tab.tab.loadEarlier();
		expect( tab.reads ).toHaveLength( 1 );
		expect( tab.reads[0].args ).toMatchObject( {end: from, limit: NodeHistory.pageSize} );
		tab.fixture.destroy();
	} );
	//with Live off a group opens on a read, as a server does, and an edit reads again;  Live on subscribes from the latest value held
	it( 'opens a group on a read with Live off, and subscribes from the latest value held when it goes on', async ()=>{
		const t = Date.now()-60_000;
		const tab = open( {candidates: [], groups: [group]} );
		tab.capabilities = {aggregates: [], update: true, delete: true};
		tab.tab.onLiveChange( false );
		expect( tab.histSubscribed() ).toBe( 0 );
		tab.tab.toggle( A ); tab.tab.toggle( A );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 1 );
		expect( tab.reads[0].source ).toEqual( {group: 3} );
		await tab.answer( {values: [at(A, t)]} );
		tab.dialogResult.answer = { values: [{node: A.nodeId, source: new Date(t), status: 0}], nodes: [] };
		await tab.tab.openEdit( 'replace', {node: A, time: new Date(t), value: 2} );
		expect( tab.reads ).toHaveLength( 2 );//the refresh
		await tab.answer( {values: [at(A, t)]} );
		tab.tab.onLiveChange( true );
		expect( tab.histSubscribed() ).toBe( 1 );
		expect( tab.histArgs().from ).toEqual( new Date(t) );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [{...at(A, t), bound: true}], more: false} );
		await tab.tab.openEdit( 'replace', {node: A, time: new Date(t), value: 3} );
		expect( tab.reads ).toHaveLength( 2 );//no refresh:  the stream pushes the modification
		tab.fixture.destroy();
	} );
	//The snapshot counted for no page:  Live off, an edit read the latest 1000 of the 2500 it held, and the trend lost the rest
	//until Load earlier paged them back (historian-live-trend #9).
	it( "reads again as many values as a group's snapshot held after an edit with Live off", async ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: Array.from( {length: 2500}, (_, i)=>at(A, 1000*(i+1)) ), more: false} );
		tab.tab.onLiveChange( false );
		tab.dialogResult.answer = { values: [{node: A.nodeId, source: new Date(5000), status: 0}], nodes: [] };
		await tab.tab.openEdit( 'replace', {node: A, time: new Date(5000), value: 2} );
		expect( tab.reads ).toHaveLength( 1 );
		expect( tab.reads[0].args.limit ).toBe( 3*NodeHistory.pageSize );
		tab.fixture.destroy();
	} );
	//Update lets the UpdateData edits and a row's Replace, Delete the purges and a row's Delete:  a group's two rights come apart,
	//where a server's HistoryWrite bit stands for both.  A row of the snapshot has its time as a Date, which no edit can name, so
	//the row actions come with a read.
	it( "offers a group's UpdateData edits under Update alone", async ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		tab.capabilities = {aggregates: [], update: true, delete: false};
		tab.emit( {kind: 'ack', statuses: [0]} );
		tab.emit( {kind: 'snapshot', values: [{...at(A, 5000), sourceTime: undefined}], more: false} );
		await settle();
		tab.fixture.detectChanges();
		expect( tab.tab.rights() ).toEqual( {update: true, delete: false} );
		expect( tab.tab.writable() ).toEqual( [A] );
		expect( tab.fixture.nativeElement.querySelector( '[aria-haspopup="menu"]' ) ).not.toBeNull();
		expect( tab.actions() ).toEqual( [] );
		tab.tab.loadEarlier();
		await tab.answer( {values: [at(A, 5000)]} );
		tab.fixture.detectChanges();
		expect( tab.actions() ).toEqual( ['Replace this value'] );
		await tab.tab.deleteValue( tab.tab.values()[0] );
		expect( tab.confirmations ).toEqual( [] );
		expect( tab.edits.sent ).toEqual( [] );
		tab.fixture.destroy();
	} );
	//The test deleted a snapshot row, which has no Delete in the app:  a snapshot's time is a Date.  The row is one Load earlier
	//read, and the push takes it (historian-live-trend #15).
	it( "offers a group's purge under Delete alone, and sends it to the group, which pushes the change", async ()=>{
		const purger = open( {candidates: [], groups: [group]} );
		purger.capabilities = {aggregates: [], update: false, delete: true};
		purger.emit( {kind: 'ack', statuses: [0]} );
		purger.emit( {kind: 'snapshot', values: [{...at(A, 5000), sourceTime: undefined}], more: false} );
		await settle();
		purger.fixture.detectChanges();
		expect( purger.actions() ).toEqual( [] );
		purger.tab.loadEarlier();
		await purger.answer( {values: [at(A, 4000)]} );
		purger.fixture.detectChanges();
		expect( purger.actions() ).toEqual( ['Delete this value'] );
		purger.tab.replaceValue( purger.tab.values()[0] );
		expect( purger.presets ).toEqual( [] );
		purger.edits.answer = { values: [], nodes: [{node: A.nodeId, status: 0}] };
		await purger.tab.deleteValue( purger.tab.values()[0] );
		expect( purger.confirmations[0] ).toContain( "the gateway's history of the group" );
		expect( purger.edits.sources ).toEqual( [{group: 3}] );
		expect( purger.edits.sent.map( e=>e.kind ) ).toEqual( ['purgeRange'] );
		expect( purger.reads ).toHaveLength( 1 );//Load earlier's, and no read after the delete:  the stream pushes the modification
		purger.emit( {kind: 'records', values: [], modifications: [{node: A.nodeId, target: new Date(4000), kind: 'delete', time: new Date(9000), user: 'me'}]} );
		expect( purger.tab.values().map( v=>v.source!.getTime() ) ).toEqual( [5000] );
		purger.fixture.destroy();
	} );
	//A child the server historizes and a group holds too:  the server's history opens, and the toggle takes the group's instead,
	//with the aggregate picked on the server dropped, since the group may not list it, and the /opc tail for the group's stream
	it( "opens on the server's history and switches to a group's, dropping the aggregate picked", async ()=>{
		const tab = open( {groups: [group]} );
		expect( tab.tab.sources().map( s=>s.label ) ).toEqual( ['Server', 'Pumps'] );
		expect( [...tab.fixture.nativeElement.querySelectorAll( '.source-group mat-button-toggle' )].map( ( b:Element )=>b.textContent?.trim() ) ).toEqual( ['Server', 'Pumps'] );
		expect( tab.reads[0].source ).toEqual( {opc: 'opc'} );
		expect( tab.subscribed() ).toBe( 1 );
		await tab.answer();
		await tab.aggregate( 'Median', 60_000 );
		await tab.answer( {values: [at(A, tab.reads[1].args.start!.getTime())]} );
		tab.tab.pick( 'group/3' );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.tab.aggregation() ).toBeUndefined();
		expect( tab.reads ).toHaveLength( 2 );
		expect( tab.subscribed() ).toBe( 0 );
		expect( tab.histSubscribed() ).toBe( 1 );
		expect( tab.histArgs().group ).toBe( 3 );
		expect( tab.tab.aggregates().map( a=>a.browse ) ).toEqual( ['Median'] );
		tab.tab.pick( 'opc/opc' );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].source ).toEqual( {opc: 'opc'} );
		expect( tab.subscribed() ).toBe( 1 );
		expect( tab.histSubscribed() ).toBe( 0 );
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
	//NodeDetail keeps the tab from one connection's node to another's (Back, Forward), and a pick another server lacks - Median
	//is OpcServer's own - failed the read there (historian-aggregate-picker #3):  the pick and the list belong to their
	//connection.  Another node of the same one keeps them.
	it( 'drops the aggregate picked on another connection, and keeps it on the same one', async ()=>{
		const tab = open();
		await tab.answer();
		await tab.aggregate( 'Median', 60_000 );
		await tab.answer( {values: [at(tab.node, tab.reads[1].args.start!.getTime())]} );
		const sibling = new Variable( <any>{ns:2, i:2, name: 'B', browse: {ns:2, name: 'B'}, historizing: true} );
		tab.fixture.componentRef.setInput( 'pageData', {gateway: tab.gateway, server: {connection: {slug: 'opc'}}, route: {profileKey: 'p'}, nodes: []} );
		tab.fixture.componentRef.setInput( 'candidates', [sibling] );
		tab.fixture.detectChanges();
		await settle();
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].args ).toMatchObject( {aggregate: 'Median', interval: 60_000} );
		await tab.answer( {values: [at(sibling, tab.reads[2].args.start!.getTime())]} );
		tab.aggregates = [{name: 'Average', browse: 'Average'}];
		tab.fixture.componentRef.setInput( 'pageData', {gateway: tab.gateway, server: {connection: {slug: 'other'}}, route: {profileKey: 'p'}, nodes: []} );
		tab.fixture.componentRef.setInput( 'candidates', [tab.node] );
		tab.fixture.detectChanges();
		expect( tab.tab.aggregation() ).toBeUndefined();
		expect( tab.tab.aggregates() ).toEqual( [] );
		await settle();
		expect( tab.reads ).toHaveLength( 4 );
		expect( tab.reads[3].args.aggregate ).toBeUndefined();
		expect( tab.tab.aggregates().map( a=>a.browse ) ).toEqual( ['Average'] );
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
		await tab.answer( {values: [at(tab.node, tab.reads[1].args.start!.getTime())]} );//data:  no read for the record before
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
	//its intervals - and no record comes before it:  where the history begins
	it( 'loads the range before an aggregate read, and stops at one with no data and no record before it', async ()=>{
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
		expect( tab.reads ).toHaveLength( 4 );
		expect( tab.reads[3].args ).toEqual( {nodes: earlier.nodes, end: earlier.start, limit: 1} );
		await tab.answer();
		expect( tab.tab.values() ).toHaveLength( 3 );
		expect( tab.tab.hasEarlier() ).toBe( false );
		tab.tab.loadEarlier();
		expect( tab.reads ).toHaveLength( 4 );
		tab.fixture.destroy();
	} );
	//A server that records on change can leave a range, the opening one included, with nothing but Bad_NoData while the node has
	//history before it (historian-aggregate-picker #1):  Load earlier goes on to the range ending with the interval of the latest
	//record before, read raw and not listed.
	it( 'reads past a range of nothing but Bad_NoData to the latest record before it', async ()=>{
		const tab = open();
		await tab.answer();
		await tab.aggregate( 'Average', 60_000 );
		const first = tab.reads[1].args;
		await tab.answer( {values: [{...at(tab.node, first.start!.getTime()), status: 0x809B0000, value: undefined}]} );
		expect( tab.reads ).toHaveLength( 3 );
		expect( tab.reads[2].args ).toEqual( {nodes: first.nodes, end: first.start, limit: 1} );
		expect( tab.tab.loading() ).toBe( true );
		const record = first.start!.getTime()-20*3_600_000;//on a minute's boundary:  the interval it starts holds it
		await tab.answer( {values: [at(tab.node, record)]} );
		expect( tab.tab.loading() ).toBe( false );
		expect( tab.tab.values() ).toHaveLength( 1 );
		expect( tab.tab.hasEarlier() ).toBe( true );
		tab.tab.loadEarlier();
		const earlier = tab.reads[3].args;
		expect( earlier ).toMatchObject( {aggregate: 'Average', interval: 60_000, limit: NodeHistory.pageSize} );
		expect( earlier.end!.getTime() ).toBe( record+60_000 );
		expect( earlier.start!.getTime() ).toBe( record+60_000-NodeHistory.pageSize*60_000 );
		tab.fixture.destroy();
	} );
	//No store holds a value before the Unix epoch, and 1000 intervals of a year went back to the year 1026, before UA's DateTime
	//begins (historian-aggregate-picker #2):  a range is the whole intervals a load holds above the floor, or starts at the floor
	//when not one fits.
	it( 'starts an aggregate range no earlier than the floor', ()=>{
		expect( NodeHistory.start( 1e9, 100, 1 ) ).toBe( 1e9-NodeHistory.pageSize*100 );
		expect( NodeHistory.start( 1e9, 100, 8 ) ).toBe( 1e9-NodeHistory.intervals( 8 )*100 );
		expect( NodeHistory.start( NodeHistory.floor+1050, 100, 1 ) ).toBe( NodeHistory.floor+50 );
		expect( NodeHistory.start( NodeHistory.floor+10, 100, 1 ) ).toBe( NodeHistory.floor );
	} );
	it( 'reads an aggregate of long intervals from the floor, with Load earlier off', async ()=>{
		const tab = open();
		await tab.answer();
		const year = 365*86_400_000;
		await tab.aggregate( 'Average', year );
		const args = tab.reads[1].args, start = args.start!.getTime(), end = args.end!.getTime();
		expect( start ).toBeGreaterThanOrEqual( NodeHistory.floor );
		expect( start ).toBeLessThan( NodeHistory.floor+year );
		expect( (end-start)%year ).toBe( 0 );
		expect( args.limit ).toBe( (end-start)/year );
		await tab.answer( {values: [at(tab.node, start)]} );
		expect( tab.tab.hasEarlier() ).toBe( false );
		tab.tab.loadEarlier();
		expect( tab.reads ).toHaveLength( 2 );
		tab.fixture.destroy();
	} );
	it( 'loads earlier down to the last whole interval above the floor', async ()=>{
		const tab = open();
		await tab.answer();
		const interval = 20*86_400_000;//a load reaches back to 1972
		await tab.aggregate( 'Average', interval );
		const first = tab.reads[1].args;
		expect( first.limit ).toBe( NodeHistory.pageSize );
		await tab.answer( {values: [at(tab.node, first.start!.getTime())]} );
		expect( tab.tab.hasEarlier() ).toBe( true );
		tab.tab.loadEarlier();
		const earlier = tab.reads[2].args, start = earlier.start!.getTime();
		expect( earlier.end ).toEqual( first.start );
		expect( start ).toBeGreaterThanOrEqual( NodeHistory.floor );
		expect( start ).toBeLessThan( NodeHistory.floor+interval );
		expect( earlier.limit ).toBe( (first.start!.getTime()-start)/interval );
		await tab.answer( {values: [at(tab.node, start)]} );
		expect( tab.tab.hasEarlier() ).toBe( false );
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
		expect( tab.tab.values() ).toHaveLength( 0 );//the range is taken in whole
		await tab.answer( {values: [at(tab.node, first.start!.getTime()+60_000)]} );
		expect( tab.tab.loading() ).toBe( false );
		expect( tab.tab.values() ).toHaveLength( 2 );
		tab.tab.loadEarlier();
		expect( tab.reads[3].args.end!.getTime() ).toBe( first.start!.getTime() );
		expect( tab.reads[3].args.continuation ).toBeUndefined();
		tab.fixture.destroy();
	} );
	//A refresh's first page replaced what was held, and an aggregate read goes forward:  the tab showed the oldest intervals
	//alone until the rest landed (historian-aggregate-picker #4).
	it( 'keeps what an aggregate refresh replaces until its last page lands', async ()=>{
		const tab = open();
		await tab.answer();
		await tab.aggregate( 'Average', 60_000 );
		const t0 = tab.reads[1].args.start!.getTime();
		await tab.answer( {values: [at(tab.node, t0), at(tab.node, t0+60_000)]} );
		tab.dialogResult.answer = { values: [{node: tab.node.nodeId, source: new Date(t0), status: 0}], nodes: [{node: tab.node.nodeId, status: 0}] };
		await tab.tab.openEdit( 'insert' );
		await tab.answer( {values: [{...at(tab.node, t0), value: 2}], continuation: 'rest'} );//the oldest interval
		expect( tab.reads.at( -1 )!.args.continuation ).toBe( 'rest' );
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [1, 1] );
		await tab.answer( {values: [{...at(tab.node, t0+60_000), value: 3}]} );
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [2, 3] );
		expect( tab.tab.hasEarlier() ).toBe( true );
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
		await tab.answer( {values: [at(tab.node, earlier.start!.getTime()), {...at(tab.node, first.start!.getTime()), value: 2}], continuation: 'rest'} );
		//the rest at the range's limit, which the gateway caps:  a page's was a tenth of its cap a round trip (historian-aggregate-picker #6)
		expect( again.limit ).toBeGreaterThan( NodeHistory.pageSize );
		expect( tab.reads[4].args ).toMatchObject( {continuation: 'rest', limit: again.limit} );
		await tab.answer();
		expect( tab.tab.values().map( v=>v.value ) ).toEqual( [1, 2] );
		expect( tab.tab.hasEarlier() ).toBe( true );
		tab.fixture.destroy();
	} );
	//The group's member list with its thresholds (#338):  a Thresholds button on a group source alone, which opens the dialog on
	//the group with the page's members to name them, loading and saving through this gateway.
	it( 'offers no thresholds on a server source', ()=>{
		const server = open();
		expect( [...server.fixture.nativeElement.querySelectorAll( 'mat-toolbar button' )].some( ( b:Element )=>b.textContent!.includes( 'Thresholds' ) ) ).toBe( false );
		server.fixture.destroy();
	} );
	it( "opens a group's thresholds on the group, through this gateway", async ()=>{
		const tab = open( {candidates: [], groups: [group]} );
		const button = [...tab.fixture.nativeElement.querySelectorAll( 'mat-toolbar button' )].find( ( b:Element )=>b.textContent!.includes( 'Thresholds' ) );
		expect( button ).toBeTruthy();
		button.click();
		expect( tab.thresholds ).toHaveLength( 1 );
		const data = tab.thresholds[0];
		expect( data.group ).toBe( 3 );
		expect( data.name ).toBe( 'Pumps' );
		expect( data.nodes.map( n=>n.key ) ).toEqual( [A.key] );
		await data.load();
		expect( tab.thresholdReads ).toEqual( [{reader: tab.gateway, group: 3}] );
		await data.save( 7, {stepped: false} );
		expect( tab.overrides ).toEqual( [{poster: tab.gateway, id: 7, changes: {stepped: false}}] );
		tab.fixture.destroy();
	} );
} );