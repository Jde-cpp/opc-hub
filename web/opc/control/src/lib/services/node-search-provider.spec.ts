import { NodeSearchProvider } from './node-search-provider';

describe( 'NodeSearchProvider.displayPath', ()=>{
	it( 'drops the namespace from every segment, as the breadcrumbs do', ()=>{
		expect( NodeSearchProvider.displayPath('5~pumps/5~pump1') ).toBe( 'pumps/pump1' );
		expect( NodeSearchProvider.displayPath('5~pumpManual') ).toBe( 'pumpManual' );
	});

	it( 'leaves a default-namespace name, and a ~ that is not a namespace, as they are', ()=>{
		expect( NodeSearchProvider.displayPath('Objects/pump~1') ).toBe( 'Objects/pump~1' );
	});
});
