import { Component, inject } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';

export type ConfirmData = {
	title:string;
	message:string;
	confirm:string;//the affirmative button's label - name the action ("Purge"), never "OK":  it is the last thing read before an irreversible step
	destructive?:boolean;//paints the affirmative button with the error colour rather than the primary
};

//The house confirmation.  Purge is the first genuinely irreversible action the UI offers - a soft delete is a `deleted`
//stamp a Restore clears, a purge is a row that stops existing - and `window.confirm` is not an option:  it blocks the
//page, cannot be styled or themed, and is invisible to the a11y pass the rest of the app is held to.
@Component({
	template: `
		<h2 mat-dialog-title>{{data.title}}</h2>
		<mat-dialog-content>{{data.message}}</mat-dialog-content>
		<mat-dialog-actions>
			<button matButton="outlined" cdkFocusInitial (click)="dialogRef.close( false )">Cancel</button><!--focus starts on Cancel: Enter on a confirmation must not be what destroys the row-->
			<button [matButton]="data.destructive ? 'filled' : 'tonal'" [class.destructive]="data.destructive" (click)="dialogRef.close( true )">{{data.confirm}}</button>
		</mat-dialog-actions>`,
	styles: `
		mat-dialog-actions{ gap: 12px; justify-content: flex-end; }
		.destructive{ background: var(--mat-sys-error); color: var(--mat-sys-on-error); }`,
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle]
})
export class ConfirmDialog{
	dialogRef = inject( MatDialogRef<ConfirmDialog,boolean> );
	data = inject<ConfirmData>( MAT_DIALOG_DATA );//an InjectionToken, so inject() takes it - unlike the string tokens elsewhere
}

//Awaitable wrapper - the callers are all `async onSomethingClick()`, and afterClosed() as an observable would push the
//action into a subscribe callback where the surrounding try/catch no longer reaches it.
export async function confirm( dialog:MatDialog, data:ConfirmData ):Promise<boolean>{
	const ref = dialog.open( ConfirmDialog, {data, width: '420px', ariaModal: true} );
	return await new Promise<boolean>( (resolve)=>ref.afterClosed().subscribe( (y)=>resolve(y===true) ) );
}