import { TestBed } from '@angular/core/testing';
import { SnackbarService, TableSchema } from 'jde-framework';
import { KeyProperties } from './key-properties';
import { snackbarStub } from 'jde-framework/testing';

const create = ( record:any )=>{
	TestBed.configureTestingModule({ providers: [ { provide: SnackbarService, useValue: snackbarStub() } ] });
	const fixture = TestBed.createComponent( KeyProperties );
	fixture.componentRef.setInput( 'record', record );
	fixture.componentRef.setInput( 'schema', {enums: new Map()} as unknown as TableSchema );
	fixture.detectChanges();
	return fixture;
};
//label → value text of one section's fact rows, buttons and icons stripped
const facts = ( fixture:ReturnType<typeof create>, title:string )=>{
	const section = [...fixture.nativeElement.querySelectorAll('.section') as NodeListOf<HTMLElement>].find( s=>s.querySelector('.section-title')?.textContent?.trim()==title )!;
	return Object.fromEntries( [...section.querySelectorAll('dt') as NodeListOf<HTMLElement>].map( dt=>{
		const dd = dt.nextElementSibling!.cloneNode( true ) as HTMLElement;
		dd.querySelectorAll( 'button' ).forEach( b=>b.remove() );
		return [dt.textContent!.trim(), dd.textContent!.replace(/\s+/g, ' ').trim()];
	}) );
};
const cert = { distinguished: "CN=gw,O=Jde-Cpp", issuer: "CN=gw,O=Jde-Cpp", subjectAlt: "URI:urn:x", expiration: "2027-08-05T09:56:02Z" };

describe( 'KeyProperties public key', ()=>{
	it( 'folds the exponent into the key line', ()=>{
		const fixture = create( { modulus: "ab".repeat(256), exponent: 65537 } );
		expect( facts(fixture, "Public key")["Key"] ).toBe( "2048-bit RSA, e = 65537" );
	} );

	it( 'dashes a row with no key', ()=>{
		expect( facts(create({}), "Public key")["Key"] ).toBe( "—" );
	} );
} );

//the cert's sha-256 in openssl's colon form - the row an operator matches against `openssl x509 -fingerprint -sha256`.
//Enrollment-time like the rest of the section, so a row enrolled before the column existed shows a dash, not an error.
describe( 'KeyProperties certificate fingerprint', ()=>{
	it( 'shows it with a copy button', ()=>{
		const fingerprint = Array.from( {length: 32}, (_, i)=>(i*8%256).toString(16).padStart(2, '0').toUpperCase() ).join( ':' );
		const fixture = create( {...cert, fingerprint} );
		expect( facts(fixture, "Certificate")["Fingerprint"] ).toBe( fingerprint );
		expect( fixture.nativeElement.querySelector('button[aria-label="Copy fingerprint"]') ).not.toBeNull();
	} );

	it( 'dashes a row enrolled before the column existed', ()=>{
		const fixture = create( cert );
		expect( facts(fixture, "Certificate")["Fingerprint"] ).toBe( "—" );
		expect( fixture.nativeElement.querySelector('button[aria-label="Copy fingerprint"]') ).toBeNull();
	} );
} );

//reviews/angular-refactor.md B17:  the expiry chip was a literal green and orange beside the theme's error token
describe( 'KeyProperties expiry chip', ()=>{
	const chip = ( days:number )=>create( {...cert, issuer: "CN=ca", expiration: new Date( Date.now()+days*86400000 ).toISOString()} ).nativeElement.querySelector( 'mat-chip.status-chip' ) as HTMLElement;//not self-signed:  that chip comes first
	it.each( [[400, 'chip-ok'], [10, 'chip-warn'], [-2, 'chip-error']] )( 'expiring in %i days takes %s, keeping its own class', ( days, tone )=>{
		expect( [...chip( days ).classList] ).toEqual( expect.arrayContaining([tone, 'status-chip']) );
	} );
} );