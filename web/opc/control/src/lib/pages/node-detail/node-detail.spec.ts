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
} );