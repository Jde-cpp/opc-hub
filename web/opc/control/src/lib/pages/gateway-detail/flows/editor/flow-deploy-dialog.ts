import { Component, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';

//`version`:  the one the deploy would write.  `fault`:  what the editor finds wrong with the graph, which the gateway decides.
export type FlowDeployDialogData = { name:string; version:number; enabled:boolean; fault?:string };

//Deploy's comment (#455):  what the version is for, kept with it.  The deploy stores the graph as the flow's next version and, for
//an enabled flow, starts it in place of the one running.  A fault the editor finds is said, and does not stop the deploy:  the
//gateway's rules are the ones that count, and it writes nothing of a graph it refuses.
@Component({
	template: `
		<h2 mat-dialog-title>Deploy {{ data.name }}</h2>
		<mat-dialog-content>
			<p>{{ data.enabled ? 'Version ' + data.version + ' is stored and started in place of the one running:  what the running flow holds is dropped.' : 'Version ' + data.version + ' is stored.  The flow is disabled, so nothing starts until it is enabled.' }}</p>
			@if( data.fault; as f ){
				<p class="problem" role="alert">As it stands the gateway would refuse it:  {{ f }}</p>
			}
			<mat-form-field subscriptSizing="dynamic">
				<mat-label>Comment</mat-label>
				<textarea matInput cdkFocusInitial rows="3" maxlength="2048" [value]="comment()" (input)="comment.set($any($event.target).value)" placeholder="What changed"></textarea>
			</mat-form-field>
		</mat-dialog-content>
		<mat-dialog-actions>
			<button matButton="outlined" type="button" (click)="dialogRef.close()">Cancel</button>
			<button matButton="filled" type="button" (click)="dialogRef.close( comment() )">Deploy</button>
		</mat-dialog-actions>`,
	styles: `
		p{ margin: 0 0 .75rem; font: var(--mat-sys-body-medium); }
		.problem{ color: var(--mat-sys-error); }
		mat-form-field{ width: 100%; }`,
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatInputModule]
})
export class FlowDeployDialog{
	dialogRef = inject<MatDialogRef<FlowDeployDialog,string|undefined>>( MatDialogRef );
	data = inject<FlowDeployDialogData>( MAT_DIALOG_DATA );
	comment = signal( '' );
	//the comment, blank for none, or undefined for a cancel
	static open( dialog:MatDialog, data:FlowDeployDialogData ):Promise<string|undefined>{
		const ref = dialog.open<FlowDeployDialog,FlowDeployDialogData,string|undefined>( FlowDeployDialog, {data, width: '520px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( y=>resolve( y ) ) );
	}
}