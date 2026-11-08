import { Component, input, output } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { UaNode } from '../../model/node';
import { NodeId } from '../../model/node-id';
import { NodeChildren } from './node-children/node-children';
import { NodeDetail } from './node-detail';
import { NodeReferences } from './node-references/node-references';
import { NodeAttributes } from './node-attributes/node-attributes';
import { NodeEvents } from './node-events/node-events';
import { NodeAlarms } from './node-alarms/node-alarms';
import { OpcStore } from '../../services/opc-store';
import { ComponentPageTitle } from 'jde-spa';
import { pageTitleStub, provideRoute, stable } from 'jde-spa/testing';
import { variable } from 'jde-opc/testing';

//stubs, not blanked components:  NodeChildren's ngOnInit loads the views and subscribes to the route's data, and NodeAttributes and
//NodeReferences read the gateway
@Component({ selector: 'node-children', template: '' })
class ChildrenStub{ node = input<UaNode>(); refreshed = output<UaNode[]>(); }
@Component({ selector: 'node-attributes', template: '' })
class AttributesStub{ node = input<UaNode>(); pageData = input<unknown>(); renamed = output<string>(); }
@Component({ selector: 'node-references', template: '' })
class ReferencesStub{ node = input<UaNode>(); pageData = input<unknown>(); }
@Component({ selector: 'node-events', template: '' })
class EventsStub{ node = input<UaNode>(); pageData = input<unknown>(); }
@Component( {selector: 'node-alarms', template: ''} )
class AlarmsStub{ node = input<UaNode>(); pageData = input<unknown>(); }

const child = ( historizing:boolean )=>variable( 1, 'a', {historizing} );

describe( 'NodeDetail', ()=>{
	//A Children refresh assigned pageData.nodes, which the History tab's gate never saw (historian-web-trend #10).
	//reviews-issue/416-address-space.md #5, #15 (browser walk):  a flip set the route's siblings again on the same object, which the
	//sidenav never repainted for
	it( 'hands the sidenav a new route when System nodes flips', ()=>{
		const route = { node: {id: 85, name: 'Objects', toJson: ()=>({ns: 0, i: 85})}, siblings: [{title: 'Objects'}] };
		const pageData = { route, nodes: [], gateway: {}, server: {applicationName: 'Other', connection: {name: 'c'}} };
		TestBed.configureTestingModule({ providers: [provideRoute( {data: {pageData}} ), pageTitleStub] });
		TestBed.overrideComponent( NodeDetail, {remove: {imports: [NodeChildren, NodeAttributes, NodeReferences, NodeEvents, NodeAlarms]}, add: {imports: [ChildrenStub, AttributesStub, ReferencesStub, EventsStub, AlarmsStub]}} );
		const fixture = TestBed.createComponent( NodeDetail );
		fixture.componentRef.setInput( 'sideNav', route );
		fixture.detectChanges();
		const store = TestBed.inject( OpcStore );
		route.siblings = [{title: 'Objects'}, {title: 'Server'}];//what the flip's setRoute does to the route the resolver gave the page
		store.setShowSystem( !store.showSystem() );
		fixture.detectChanges();
		const shown = fixture.componentInstance.sideNav();
		expect( shown ).not.toBe( <any>route );
		expect( shown.node ).toBe( <any>route.node );
		expect( shown.siblings!.map( s=>s.title ) ).toEqual( ['Objects', 'Server'] );
		store.setShowSystem( false );
		fixture.destroy();
	} );

	it( 'shows the History tab when a Children refresh brings a historized node', ()=>{
		const route = { node: {id: 85, name: 'Objects', toJson: ()=>({ns: 0, i: 85})} };
		const pageData = { route, nodes: [child( false )], gateway: {}, server: {applicationName: 'Other', connection: {name: 'c'}} };
		TestBed.configureTestingModule({ providers: [
			provideRoute( {data: {pageData}} ),
			pageTitleStub
		]});
		TestBed.overrideComponent( NodeDetail, {remove: {imports: [NodeChildren, NodeAttributes, NodeReferences, NodeEvents, NodeAlarms]}, add: {imports: [ChildrenStub, AttributesStub, ReferencesStub, EventsStub, AlarmsStub]}} );
		const fixture = TestBed.createComponent( NodeDetail );
		fixture.componentRef.setInput( 'sideNav', route );
		fixture.detectChanges();
		const tabs = ()=>[...fixture.nativeElement.querySelectorAll( '[role="tab"]' )].map( t=>t.textContent.trim() );
		expect( tabs() ).toEqual( ['Children', 'Attributes', 'References'] );

		fixture.debugElement.query( By.directive(ChildrenStub) ).componentInstance.refreshed.emit( [child( true )] );
		fixture.detectChanges();
		expect( tabs() ).toEqual( ['Children', 'Attributes', 'References', 'History'] );
		fixture.destroy();
	} );
	//A group's member needn't be one the server historizes (#335):  the tab shows for it, with the group's members among the
	//page's variables as a source, and a group none of the page's nodes are in is no source.
	it( 'shows the History tab for a child a gateway group holds, with the group as a source', ()=>{
		const route = { node: {id: 85, name: 'Objects', toJson: ()=>({ns: 0, i: 85})} };
		const member = child( false ), other = variable( 2, 'b' );
		const groups = [{id: 3, name: 'Pumps', nodes: [member.nodeId]}, {id: 4, name: 'Empty', nodes: [new NodeId( {ns: 2, i: 9} )]}];
		const pageData = { route, nodes: [member, other], gateway: {}, server: {applicationName: 'Other', connection: {name: 'c'}}, groups };
		TestBed.configureTestingModule({ providers: [
			provideRoute( {data: {pageData}} ),
			pageTitleStub
		]});
		TestBed.overrideComponent( NodeDetail, {remove: {imports: [NodeChildren, NodeAttributes, NodeReferences, NodeEvents, NodeAlarms]}, add: {imports: [ChildrenStub, AttributesStub, ReferencesStub, EventsStub, AlarmsStub]}} );
		const fixture = TestBed.createComponent( NodeDetail );
		fixture.componentRef.setInput( 'sideNav', route );
		fixture.detectChanges();
		expect( [...fixture.nativeElement.querySelectorAll( '[role="tab"]' )].map( t=>t.textContent.trim() ) ).toEqual( ['Children', 'Attributes', 'References', 'History'] );
		expect( fixture.componentInstance.historizable() ).toEqual( [] );
		expect( fixture.componentInstance.groups().map( g=>[g.id, g.name, g.nodes.map( n=>n.name )] ) ).toEqual( [[3, 'Pumps', ['a']]] );
		fixture.destroy();
	} );
	//reviews-issue/417-clients-attributes.md #7:  a DisplayName written on the Attributes tab kept the old name in the title and the
	//sidenav until a reload
	it( 'takes a name the Attributes tab wrote into the title and the sidenav', async ()=>{
		const node = {id: 5005, name: 'Pump (Manual)', toJson: ()=>({ns: 2, i: 5005})};
		const route = { node, siblings: [{title: 'Pump (Manual)'}, {title: 'Pump 1'}] };
		const pageData = { route, nodes: [], gateway: {}, server: {applicationName: 'Other', connection: {name: 'c'}} };
		TestBed.configureTestingModule({ providers: [provideRoute( {data: {pageData}} ), pageTitleStub] });
		TestBed.overrideComponent( NodeDetail, {remove: {imports: [NodeChildren, NodeAttributes, NodeReferences, NodeEvents, NodeAlarms]}, add: {imports: [ChildrenStub, AttributesStub, ReferencesStub, EventsStub, AlarmsStub]}} );
		const fixture = TestBed.createComponent( NodeDetail );
		fixture.componentRef.setInput( 'sideNav', route );
		fixture.componentInstance.tabIndex.set( 1 );//the Attributes tab, whose content is lazy
		await stable( fixture );
		const store = TestBed.inject( OpcStore );
		const refresh = vi.spyOn( store, 'refreshRoute' ).mockImplementation( ()=>{ route.siblings = [{title: node.name}, {title: 'Pump 1'}]; } );//what setRoute does with the renamed store node
		const title = TestBed.inject( ComponentPageTitle );
		expect( title.title ).toBe( 'c/Pump (Manual)' );
		fixture.debugElement.query( By.directive(AttributesStub) ).componentInstance.renamed.emit( 'Pump One' );
		fixture.detectChanges();
		expect( node.name ).toBe( 'Pump One' );
		expect( refresh ).toHaveBeenCalled();
		const shown = fixture.componentInstance.sideNav();
		expect( shown ).not.toBe( <any>route );
		expect( shown.siblings!.map( s=>s.title ) ).toEqual( ['Pump One', 'Pump 1'] );
		expect( title.title ).toBe( 'c/Pump One' );
		fixture.componentInstance.tabIndex.set( 0 );
		fixture.detectChanges();
		fixture.destroy();
	} );
	//clients spec *Events* (#424) and *Alarms and conditions* (#426):  the Events and Alarms tabs on a node whose EventNotifier has the
	//SubscribeToEvents bit, and on no other
	it( 'shows the Events and Alarms tabs for a node that notifies events', ()=>{
		const tabsOf = ( node:any )=>{
			const route = { node };
			const pageData = { route, nodes: [], gateway: {}, server: {applicationName: 'Other', connection: {name: 'c'}} };
			TestBed.configureTestingModule({ providers: [provideRoute( {data: {pageData}} ), pageTitleStub] });
			TestBed.overrideComponent( NodeDetail, {remove: {imports: [NodeChildren, NodeAttributes, NodeReferences, NodeEvents, NodeAlarms]}, add: {imports: [ChildrenStub, AttributesStub, ReferencesStub, EventsStub, AlarmsStub]}} );
			const fixture = TestBed.createComponent( NodeDetail );
			fixture.componentRef.setInput( 'sideNav', route );
			fixture.detectChanges();
			const tabs = [...fixture.nativeElement.querySelectorAll( '[role="tab"]' )].map( t=>t.textContent.trim() );
			fixture.destroy();
			TestBed.resetTestingModule();//two pages in one test
			return tabs;
		};
		expect( tabsOf( {id: 2253, name: 'Server', notifiesEvents: true, toJson: ()=>({ns: 0, i: 2253})} ) ).toEqual( ['Children', 'Attributes', 'References', 'Events', 'Alarms'] );
		expect( tabsOf( {id: 85, name: 'Objects', notifiesEvents: false, toJson: ()=>({ns: 0, i: 85})} ) ).toEqual( ['Children', 'Attributes', 'References'] );
	} );
} );