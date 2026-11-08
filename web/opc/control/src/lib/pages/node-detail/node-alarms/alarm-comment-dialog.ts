import { Component, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';

//what the dialog opens on:  the action - Acknowledge, Confirm or Comment - and the condition it is for, with its message
export type AlarmCommentDialogData = { action:string; condition:string; message:string };

//The comment an Acknowledge, Confirm or AddComment carries (clients spec *Alarms and conditions*, #426):  one text box, empty allowed,
//since Part 9 takes a null comment as none.  Closes with the text, or with nothing on Cancel.
@Component({
	template: `
		<h2 mat-dialog-title>{{ data.action }} {{ data.condition }}</h2>
		<mat-dialog-content>
			@if( data.message ){
				<p class="hint">{{ data.message }}</p>
			}
			<mat-form-field class="comment" subscriptSizing="dynamic">
				<mat-label>Comment</mat-label>
				<textarea matInput rows="3" cdkFocusInitial [value]="comment()" (input)="comment.set($any($event.target).value)" placeholder="Why, or what was done"></textarea>
			</mat-form-field>
		</mat-dialog-content>
		<mat-dialog-actions>
			<button matButton="outlined" type="button" (click)="dialogRef.close()">Cancel</button>
			<button matButton="tonal" type="button" (click)="dialogRef.close(comment())">{{ data.action }}</button>
		</mat-dialog-actions>`,
	styles: `
		.hint{ margin: 0 0 1rem; color: var(--mat-sys-on-surface-variant); font: var(--mat-sys-body-medium); }
		.comment{ display: block; width: 100%; }
		mat-dialog-actions{ gap: 12px; justify-content: flex-end; }`,
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatInputModule]
})
export class AlarmCommentDialog{
	dialogRef = inject<MatDialogRef<AlarmCommentDialog,string|undefined>>( MatDialogRef );
	data = inject<AlarmCommentDialogData>( MAT_DIALOG_DATA );
	comment = signal( "" );
	static open( dialog:MatDialog, data:AlarmCommentDialogData ):Promise<string|undefined>{
		const ref = dialog.open<AlarmCommentDialog,AlarmCommentDialogData,string|undefined>( AlarmCommentDialog, {data, width: '420px', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( r=>resolve( r ?? undefined ) ) );
	}
}