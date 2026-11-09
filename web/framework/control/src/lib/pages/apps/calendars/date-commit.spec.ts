import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { DateCommit } from './date-commit';

@Component( {template: '<input type="date" (dateCommit)="commits()">', imports: [DateCommit]} )
class Host{ commits = vi.fn(); }

//the field over its host, `key`, `change` and `leave` as a browser sends them
const create = ()=>{
	const fixture = TestBed.createComponent( Host );
	fixture.detectChanges();
	const input:HTMLInputElement = fixture.nativeElement.querySelector( 'input' );
	return {
		commits: fixture.componentInstance.commits, input,
		key: ( key:string )=>input.dispatchEvent( new KeyboardEvent( 'keydown', {key} ) ),
		change: ()=>input.dispatchEvent( new Event( 'change' ) ),
		leave: ()=>input.dispatchEvent( new Event( 'blur' ) )
	};
};

describe( 'DateCommit', ()=>{
	it( 'holds what is typed until the field is left', ()=>{
		const d = create();
		for( const digit of '12152026' ){
			d.key( digit );
			d.change();
		}
		expect( d.commits ).not.toHaveBeenCalled();
		d.leave();
		expect( d.commits ).toHaveBeenCalledTimes( 1 );
		d.leave();
		expect( d.commits ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'takes what is typed on Enter', ()=>{
		const d = create();
		d.key( 'ArrowUp' );
		d.change();
		d.key( 'Enter' );
		expect( d.commits ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'takes nothing where nothing changed', ()=>{
		const d = create();
		d.key( '1' );
		d.leave();
		d.key( 'Enter' );
		expect( d.commits ).not.toHaveBeenCalled();
	} );
	it( "takes the popup's pick at once:  with no key to it, after a space that opened it, or a click after typing", ()=>{
		const d = create();
		d.change();
		expect( d.commits ).toHaveBeenCalledTimes( 1 );
		d.key( ' ' );
		d.change();
		expect( d.commits ).toHaveBeenCalledTimes( 2 );
		d.key( '1' );
		d.input.click();
		d.change();
		expect( d.commits ).toHaveBeenCalledTimes( 3 );
	} );
} );