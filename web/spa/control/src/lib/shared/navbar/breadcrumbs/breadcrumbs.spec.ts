import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { RouteItem } from '../../../pages/component-sidenav/route-item';
import { Breadcrumbs } from './breadcrumbs';
import { Blank } from 'jde-spa/testing';

//reviews/m3-closing.md #8:  a crumb's path is cut from the serialized - percent-encoded - url, and a string routerLink
//encodes its '%' again:  the crumb back up to KEPServerEX's `Simulation Examples` linked to `Simulation%2520Examples`.
describe( 'Breadcrumbs', ()=>{
	it( 'links a crumb whose segment is encoded back to that segment', ()=>{
		TestBed.configureTestingModule( {providers: [
			provideRouter( [{ path: 'gateways/:gateway/:connection', component: Blank, children: [{path: '**', component: Blank}] }] )
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