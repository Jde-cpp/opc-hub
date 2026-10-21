import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { Failure } from '../../utils/errors';
import { FailureState } from './failure-state';

//reviews/angular-refactor.md A6:  cards, ql-list and resource-detail each wrote this block.
@Component( {
	imports: [FailureState],
	template: `<failure-state [failure]="failure()" [retryable]="retryable()" (retry)="retries=retries+1">@if( help() ){ <a class="help">Help</a> }</failure-state>`
})
class Host{
	failure = signal<Failure>( {kind: "failed", title: "Could not load users.", detail: "gateway down"} );
	retryable = signal( true );
	help = signal( false );
	retries = 0;
}

describe( 'FailureState', ()=>{
	const create = ( set:( h:Host )=>void = ()=>{} )=>{
		const fixture = TestBed.createComponent( Host );
		set( fixture.componentInstance );
		fixture.detectChanges();
		return { host: fixture.componentInstance, root: fixture.nativeElement as HTMLElement };
	};
	const retry = ( root:HTMLElement )=>root.querySelector<HTMLButtonElement>( '.actions button' );

	it( 'shows a failure with Retry', ()=>{
		const { host, root } = create();
		expect( root.querySelector('mat-icon')!.textContent ).toBe( "error_outline" );
		expect( root.querySelector('.title')!.textContent ).toBe( "Could not load users." );
		expect( root.querySelector('.detail')!.textContent ).toBe( "gateway down" );
		retry( root )!.click();
		expect( host.retries ).toBe( 1 );
	} );

	it( 'offers no Retry on a refusal, nor where the page has nothing to retry', ()=>{
		const forbidden = create( h=>h.failure.set({kind: "forbidden", title: "No access.", detail: ""}) ).root;
		expect( forbidden.querySelector('mat-icon')!.textContent ).toBe( "lock" );
		expect( retry(forbidden) ).toBeNull();
		expect( forbidden.querySelector('.detail') ).toBeNull();
		expect( retry(create( h=>h.retryable.set(false) ).root) ).toBeNull();
	} );

	it( "puts the page's Help in the action row", ()=>{
		const { root } = create( h=>h.help.set(true) );
		expect( [...root.querySelector('.actions')!.children].map( c=>c.textContent!.trim() ) ).toEqual( ["refreshRetry", "Help"] );
	} );
} );