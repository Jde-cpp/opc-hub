import { SnackbarService } from 'jde-framework';
import { HistMembers } from './hist-members';

//A19:  the Members-tab plumbing the template and group pages share.
describe( 'HistMembers', ()=>{
	const create = ()=>{
		const snackbar = { exception: vi.fn(), error: vi.fn(), info: vi.fn() };
		return { members: new HistMembers<number>( snackbar as unknown as SnackbarService ), snackbar };
	};

	it( 'lands a read only while the page still shows its record', async ()=>{
		const { members } = create();
		let current = true;
		await members.read( async ()=>[1, 2], rows=>members.rows.set( rows ), ()=>current );
		expect( members.rows() ).toEqual( [1, 2] );
		current = false;
		await members.read( async ()=>[3], rows=>members.rows.set( rows ), ()=>current );
		expect( members.rows() ).toEqual( [1, 2] );
		expect( members.loading() ).toBe( false );
	} );

	it( 'says a refused read on the tab, and a refused edit as a toast', async ()=>{
		const { members, snackbar } = create();
		await members.read( ()=>Promise.reject( new Error( 'no Read' ) ), ()=>{}, ()=>true );
		expect( members.error() ).toBe( 'no Read' );
		expect( members.loading() ).toBe( false );
		let busy:boolean|undefined;
		await members.mutate( async ()=>{ busy = members.busy(); throw new Error( 'no Delete' ); }, 'Could not remove the node.' );
		expect( busy ).toBe( true );
		expect( members.busy() ).toBe( false );
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not remove the node.', expect.any( Error ) );
	} );
} );