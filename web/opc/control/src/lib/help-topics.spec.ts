import { helpTopicFor } from 'jde-spa';
import { opcHelpTopics } from './help-topics';

//Which topic the navbar's ? opens for a url under a gateway's page:  a list's own where it has one, else the gateways topic.
describe( 'opcHelpTopics', ()=>{
	const topic = ( url:string )=>helpTopicFor( opcHelpTopics, url.split( '/' ) )?.id;

	it( "opens a page's own topic, and the gateways topic for the rest", ()=>{
		expect( topic( 'apps/gateways/1/flows/pump1' ) ).toBe( 'gateways/flows' );
		expect( topic( 'apps/gateways/1/groups/pumps' ) ).toBe( 'gateways/groups' );
		expect( topic( 'apps/gateways/1/machines/filler/oee' ) ).toBe( 'gateways/oee' );
		expect( topic( 'apps/gateways/1' ) ).toBe( 'gateways' );
		expect( topic( 'apps/gateways/1/OpcServer/pump1' ) ).toBe( 'gateways' );
	} );
	it( 'serves each topic from the help assets, under an id of its own', ()=>{
		expect( opcHelpTopics.every( t=>/^assets\/jde-opc\/help\/[\w-]+\.md$/.test( t.url ) ) ).toBe( true );
		expect( new Set( opcHelpTopics.map( t=>t.id ) ).size ).toBe( opcHelpTopics.length );
	} );
} );