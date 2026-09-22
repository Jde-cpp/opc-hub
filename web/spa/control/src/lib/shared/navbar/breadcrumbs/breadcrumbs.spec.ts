if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { RouteItem } from '../../../pages/component-sidenav/route-item';
import { Breadcrumbs } from './breadcrumbs';

@Component( {template: ''} ) class Dummy{}

//reviews/m3-closing.md #8:  a crumb's path is cut from the serialized - percent-encoded - url, and a string routerLink
//encodes its '%' again:  the crumb back up to KEPServerEX's `Simulation Examples` linked to `Simulation%2520Examples`.
describe( 'Breadcrumbs', ()=>{
	it( 'links a crumb whose segment is encoded back to that segment', ()=>{
		TestBed.configureTestingModule( {providers: [
			provideRouter( [{ path: 'gateways/:gateway/:connection', component: Dummy, children: [{path: '**', component: Dummy}] }] )
		]} );
		const fixture = TestBed.createComponent( Breadcrumbs );
		fixture.componentRef.setInput( 'crumbs', [
			new RouteItem( {path: '/', title: 'Home'} ),
			new RouteItem( {path: '/gateways/gw/kep/Simulation%20Examples', title: 'Simulation Examples'} ),
			new RouteItem( {title: 'Functions'} )
		] );
		fixture.detectChanges();
		const hrefs = [...fixture.nativeElement.querySelectorAll('a')].map( (a:HTMLAnchorElement)=>a.getAttribute('href') );
		expect( hrefs ).toEqual( ['/', '/gateways/gw/kep/Simulation%20Examples'] );
	} );
} );
