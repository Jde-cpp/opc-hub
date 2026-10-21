import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { RecentVisits } from 'jde-spa';
import { RecentRow } from './recent-row';
import { Blank } from 'jde-spa/testing';

//reviews/m3-closing.md #8:  a visit is stored as the url the router serialized - percent-encoded - and a string routerLink
//splits it on '/' and pushes each part as a raw segment, so the serializer encoded the '%' again:  a node under a browse
//name with a space (KEPServerEX's `Simulation Examples`) got a tile linking to `Simulation%2520Examples`, which is not found.
describe( 'RecentRow', ()=>{
	it( 'links a visited url with an encoded segment back to that url', ()=>{
		const url = '/gateways/gw/kep/Simulation%20Examples/Functions';
		TestBed.configureTestingModule( {providers: [
			provideRouter( [{ path: 'gateways/:gateway/:connection', component: Blank, children: [{path: '**', component: Blank}] }] ),
			{provide: RecentVisits, useValue: {visits: signal([{url, title: 'Functions', path: 'Gateways › gw', section: 'gateways', at: Date.now()}]), load: async ()=>{}, clear: ()=>{}}}
		]} );
		const fixture = TestBed.createComponent( RecentRow );
		fixture.detectChanges();
		expect( fixture.nativeElement.querySelector('a.recent-card').getAttribute('href') ).toBe( url );
	} );
} );