import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MAT_TABS_CONFIG } from '@angular/material/tabs';
import { Mutation, Properties } from 'jde-framework';
import { OeeReason } from '../../../model/oee-admin';
import { OeeReasonDetail } from './oee-reason-detail';
import { flush } from 'jde-spa/testing';
import { configureDetailPage } from 'jde-framework/testing';

//the page over a stub gateway, whose `mutate` throws `refusal` when there is one
const create = async ( pageRow:any, refusal?:string )=>{
	const gateway = { slug: 'g', mutate: vi.fn( async ( _m:Mutation[]|string )=>{ if( refusal ) throw new Error( refusal ); } ) };
	const {snackbar, router} = configureDetailPage( pageRow, {gateway, providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },
		{ provide: MAT_TABS_CONFIG, useValue: {animationDuration: '0ms'} }
	]} );
	TestBed.overrideComponent( Properties, {set: {template: '', imports: []}} );//the form reads a whole schema
	const fixture = TestBed.createComponent( OeeReasonDetail );
	fixture.detectChanges();
	await flush( 3 );
	return { page: fixture.componentInstance, fixture, gateway, snackbar, router };
};

describe( 'OeeReasonDetail', ()=>{
	it( 'saves a new reason with its category and code', async ()=>{
		const d = await create( {} );
		expect( d.page.picks['category'].map( p=>p.value ) ).toEqual( ['Unplanned', 'Planned', 'Excluded'] );
		d.page.properties.set( Properties.edited( OeeReason, d.page.properties(), {name: 'Jam', slug: 'jam', category: 'Unplanned', code: '7'} ) );
		await d.page.onSubmitClick();
		expect( d.gateway.mutate.mock.calls[0][0].toString() ).toBe( 'createOeeReason( slug:"jam", name:"Jam", category:"Unplanned", code:7 )' );
		expect( d.router.navigate ).toHaveBeenCalled();
	} );
	it( "shows a refusal as the form's error, until the form is edited", async ()=>{
		const refusal = "code 7 is reason 'Jam's:  a code names one reason.";
		const d = await create( {id: 3, name: 'Wrap', slug: 'wrap', category: 'Planned', code: null}, refusal );
		d.page.properties.set( Properties.edited( OeeReason, d.page.properties(), {code: '7'} ) );
		await d.page.onSubmitClick();
		d.fixture.detectChanges();
		expect( d.fixture.nativeElement.querySelector( '.refused[role=alert]' ).textContent ).toContain( refusal );
		expect( d.router.navigate ).not.toHaveBeenCalled();
		expect( d.snackbar.exception ).not.toHaveBeenCalled();
		d.page.properties.set( Properties.edited( OeeReason, d.page.properties(), {code: '8'} ) );
		expect( d.page.saveError() ).toBeUndefined();
	} );
} );