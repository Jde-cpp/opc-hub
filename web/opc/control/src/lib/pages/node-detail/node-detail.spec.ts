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
import { ActivatedRoute } from '@angular/router';
import { of } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { UaNode, Variable } from '../../model/node';
import { NodeId } from '../../model/node-id';
import { NodeChildren } from './node-children/node-children';
import { NodeDetail } from './node-detail';

@Component({ selector: 'node-children', template: '' })
class ChildrenStub{ node = input<UaNode>(); refreshed = output<UaNode[]>(); }

const variable = ( historizing:boolean )=>new Variable( <any>{ns:2, i:1, name: 'a', browse: {ns:2, name: 'a'}, historizing} );

describe( 'NodeDetail', ()=>{
	//A Children refresh assigned pageData.nodes, which the History tab's gate never saw (historian-web-trend #10).
	it( 'shows the History tab when a Children refresh brings a historized node', ()=>{
		const route = { node: {id: 85, name: 'Objects', toJson: ()=>({ns: 0, i: 85})} };
		const pageData = { route, nodes: [variable( false )], gateway: {}, server: {applicationName: 'Other', connection: {name: 'c'}} };
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: of({pageData})} },
			{ provide: ComponentPageTitle, useValue: {} }
		]});
		TestBed.overrideComponent( NodeDetail, {remove: {imports: [NodeChildren]}, add: {imports: [ChildrenStub]}} );
		const fixture = TestBed.createComponent( NodeDetail );
		fixture.componentRef.setInput( 'sideNav', route );
		fixture.detectChanges();
		const tabs = ()=>[...fixture.nativeElement.querySelectorAll( '[role="tab"]' )].map( t=>t.textContent.trim() );
		expect( tabs() ).toEqual( ['Children'] );

		fixture.debugElement.query( By.directive(ChildrenStub) ).componentInstance.refreshed.emit( [variable( true )] );
		fixture.detectChanges();
		expect( tabs() ).toEqual( ['Children', 'History'] );
		fixture.destroy();
	} );
	//A group's member needn't be one the server historizes (#335):  the tab shows for it, with the group's members among the
	//page's variables as a source, and a group none of the page's nodes are in is no source.
	it( 'shows the History tab for a child a gateway group holds, with the group as a source', ()=>{
		const route = { node: {id: 85, name: 'Objects', toJson: ()=>({ns: 0, i: 85})} };
		const member = variable( false ), other = new Variable( <any>{ns:2, i:2, name: 'b', browse: {ns:2, name: 'b'}} );
		const groups = [{id: 3, name: 'Pumps', nodes: [member.nodeId]}, {id: 4, name: 'Empty', nodes: [new NodeId( {ns: 2, i: 9} )]}];
		const pageData = { route, nodes: [member, other], gateway: {}, server: {applicationName: 'Other', connection: {name: 'c'}}, groups };
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: of({pageData})} },
			{ provide: ComponentPageTitle, useValue: {} }
		]});
		TestBed.overrideComponent( NodeDetail, {remove: {imports: [NodeChildren]}, add: {imports: [ChildrenStub]}} );
		const fixture = TestBed.createComponent( NodeDetail );
		fixture.componentRef.setInput( 'sideNav', route );
		fixture.detectChanges();
		expect( [...fixture.nativeElement.querySelectorAll( '[role="tab"]' )].map( t=>t.textContent.trim() ) ).toEqual( ['Children', 'History'] );
		expect( fixture.componentInstance.historizable() ).toEqual( [] );
		expect( fixture.componentInstance.groups().map( g=>[g.id, g.name, g.nodes.map( n=>n.name )] ) ).toEqual( [[3, 'Pumps', ['a']]] );
		fixture.destroy();
	} );
} );