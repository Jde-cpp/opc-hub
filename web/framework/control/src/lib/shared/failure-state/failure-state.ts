import { Component, input, output } from '@angular/core';
import { MatButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { Failure } from '../../utils/errors';

//A failureState() as the page's `jde-empty` block:  the lock or the error icon, the title, the throw's words, and Retry for a
//failure (a refusal would only be refused again).  What else the action row offers - a Help link - is projected.
@Component( {
	selector: 'failure-state',
	host: {style: 'display: contents'},
	template: `<div class="jde-empty" role="alert">
		<mat-icon aria-hidden="true">{{ failure().kind=='forbidden' ? 'lock' : 'error_outline' }}</mat-icon>
		<p class="title">{{ failure().title }}</p>
		@if( failure().detail ){ <p class="detail">{{ failure().detail }}</p> }
		<div class="actions">
			@if( retryable() && failure().kind!='forbidden' ){
				<button matButton="tonal" (click)="retry.emit()"><mat-icon>refresh</mat-icon>Retry</button>
			}
			<ng-content/>
		</div>
	</div>`,
	styles: ".actions:empty{ display: none; }",
	imports: [MatButton, MatIcon]
})
export class FailureState{
	failure = input.required<Failure>();
	retryable = input<boolean>( true );//false where the page has nothing to retry with
	retry = output<void>();
}