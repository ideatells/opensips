/*
 * $Id$
 *
 * pua_mi module - MI pua module
 *
 * Copyright (C) 2006 Voice Sistem S.R.L.
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>

#include "../../parser/parse_expires.h"
#include "../../parser/parse_uri.h"
#include  "../../mem/mem.h"
#include "../../mi/mi.h"
#include "../../ut.h"

#include "../pua/pua_bind.h"
#include "../pua/pua.h"
#include "pua_mi.h"

/*
 * mi cmd: pua_publish
 *		<presentity_uri> 
 *		<expires>
 *		<event package>
 *		<content_type>     - body type if body of a type different from default
 *                            event content-type or . 
 *		<ETag>             - ETag that publish should match or . if no ETag
 *		<publish_body>     - may not be present in case of update for expire
 */
int mi_publ_rpl_cback(struct sip_msg* reply, void* param);

struct mi_root* mi_pua_publish(struct mi_root* cmd, void* param)
{
	int len;
	struct mi_node* node= NULL;
	str pres_uri, expires;
	str body= {0, 0};
	struct sip_uri uri;
	publ_info_t publ;
	str event;
	str content_type;
	str etag;
	int result;

	DBG("DEBUG:pua_mi:mi_pua_publish: start\n");

	node = cmd->node.kids;
	if(node == NULL)
		return 0;

	/* Get presentity URI */
	pres_uri = node->value;
	if(pres_uri.s == NULL || pres_uri.s== 0)
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: empty uri\n");
		return init_mi_tree(404, "Empty presentity URI", 20);
	}
	if(parse_uri(pres_uri.s, pres_uri.len, &uri)<0 )
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: bad uri\n");
		return init_mi_tree(404, "Bad presentity URI", 18);
	}
	DBG("DEBUG:pua_mi:mi_pua_publish: pres_uri '%.*s'\n",
	    pres_uri.len, pres_uri.s);

	node = node->next;
	if(node == NULL)
		return 0;

	/* Get expires */
	expires= node->value;
	if(expires.s== NULL || expires.len== 0)
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: "
		    "empty expires parameter\n");
		return init_mi_tree(400, "Empty expires parameter", 23);
	}
	if( str2int(&expires, (unsigned int*) &len)< 0)
	{
		LOG(L_ERR,"ERROR;pua_mi:mi_pua_publish: "
		    "invalid expires parameter\n" );
		goto error;
	}
	DBG("DEBUG:pua_mi:mi_pua_publish: expires '%d'\n", len);

	node = node->next;
	if(node == NULL)
		return 0;

	/* Get event */
	event= node->value;
	if(event.s== NULL || event.len== 0)
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: "
		    "empty event parameter\n");
		return init_mi_tree(400, "Empty event parameter", 21);
	}
	DBG("DEBUG:pua_mi:mi_pua_publish: event '%.*s'\n",
	    event.len, event.s);

	node = node->next;
	if(node == NULL)
		return 0;

	/* Get content type */
	content_type= node->value;
	if(content_type.s== NULL || content_type.len== 0)
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: "
		    "empty content type\n");
		return init_mi_tree(400, "Empty content type parameter", 28);
	}
	DBG("DEBUG:pua_mi:mi_pua_publish: content type '%.*s'\n",
	    content_type.len, content_type.s);

	node = node->next;
	if(node == NULL)
		return 0;

	/* Get etag */
	etag= node->value;
	if(etag.s== NULL || etag.len== 0)
	{
		LOG(L_ERR, "ERROR:pua_mi: mi_pua_publish: "
		    "empty etag parameter\n");
		return init_mi_tree(400, "Bad expires", 11);
	}
	DBG("DEBUG:pua_mi:mi_pua_publish: etag '%.*s'\n",
	    etag.len, etag.s);

	node = node->next;

	/* Get body */
	if(node == NULL )
	{
		body.s= NULL;
		body.len= 0;
	}
	else
	{
		if(node->next!=NULL)
			return init_mi_tree(400, "Too many parameters", 19);

		body= node->value;
		if(body.s == NULL || body.s== 0)
		{
			LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: "
			    "empty body parameter\n");
			return init_mi_tree(400, "Empty body parameter", 20);
		}
	}
	DBG("DEBUG:pua_mi:mi_pua_publish: body '%.*s'\n",
	    body.len, body.s);

	/* Check that body is NULL iff content type is . */
	if(body.s== NULL && (content_type.len!= 1 || content_type.s[0]!= '.'))
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: "
			    "body is missing, but content type is not .\n");
		return init_mi_tree(400, "Body parameter is missing", 25);
	}

	/* Create the publ_info_t structure */
	memset(&publ, 0, sizeof(publ_info_t));
	
	publ.pres_uri= &pres_uri;
	if(body.s)
	{
		publ.body= &body;
	}
	
	publ.event= get_event_flag(&event);
	if(publ.event< 0)
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: "
			    "unkown event\n");
		return init_mi_tree(400, "Unknown event", 13);
	}
	if(content_type.len!= 1)
	{
		publ.content_type= content_type;
	}	
	
	if(! (etag.len== 1 && etag.s[0]== '.'))
	{
		publ.etag= &etag;
	}	
	publ.expires= len;
	publ.source_flag|= MI_PUBLISH;
	
	if (cmd->async_hdl!=NULL)
	{
		publ.cbrpl= mi_publ_rpl_cback;
		publ.cbparam= (void*)cmd->async_hdl;
	}	

	DBG("DEBUG:pua_mi:mi_pua_publish: send publish\n");

	result= pua_send_publish(&publ);

	if(result< 0)
	{
		LOG(L_ERR, "ERROR:pua_mi:mi_pua_publish: "
		    "sending publish failed\n");
		return init_mi_tree(500, "MI/PUBLISH failed", 17);
	}	
	if(result== 418)
		return init_mi_tree(418, "Wrong ETag", 10);
	
	if (cmd->async_hdl==NULL)
			return init_mi_tree( 202, "Accepted", 8);
	else
			return MI_ROOT_ASYNC_RPL;

error:

	return 0;
}

int mi_publ_rpl_cback(struct sip_msg* reply, void* param)
{
	struct mi_root *rpl_tree= NULL;
	struct mi_handler* mi_hdl= NULL;
	struct hdr_field* hdr= NULL;
	int statuscode;
	int lexpire;
	int found;
	str etag;

	if(reply== NULL || param== NULL)
	{
		LOG(L_ERR, "pua_mi:mi_publ_rpl_cback: ERROR NULL parameter\n");
		return -1;
	}
	if(reply== FAKED_REPLY)
		return 0;

	mi_hdl = (struct mi_handler *)(param);
	statuscode= reply->first_line.u.reply.statuscode;

	/* extract ETag and expires */
	lexpire = ((exp_body_t*)reply->expires->parsed)->val;
	DBG("PUA:mi_publ_rpl_cback: lexpire= %d\n", lexpire);
	
	hdr = reply->headers;
	while (hdr!= NULL)
	{
		if(strncmp(hdr->name.s, "SIP-ETag",8)==0 )
		{
			found = 1;
			break;
		}
		hdr = hdr->next;
	}
	if(found== 0) /* must find SIP-Etag header field in 200 OK msg*/
	{	
		LOG(L_ERR, "PUA:mi_publ_rpl_cback: SIP-ETag header field not found\n");
		goto error;
	}
	etag= hdr->body;

	rpl_tree = init_mi_tree( 200, MI_OK_S, MI_OK_LEN);
	if (rpl_tree==0)
		goto done;

		
	addf_mi_node_child( &rpl_tree->node, 0, 0, 0, "%d %.*s",
			statuscode, reply->first_line.u.reply.reason.len,
			reply->first_line.u.reply.reason.s);
	
	addf_mi_node_child( &rpl_tree->node, 0, "ETag", 4, "%.*s", etag.len, etag.s);
	
	addf_mi_node_child( &rpl_tree->node, 0, "Expires", 7, "%d", lexpire);

done:
	if ( statuscode >= 200) 
	{
		mi_hdl->handler_f( rpl_tree, mi_hdl, 1);
		param= 0;
	}
	else 
	{
		mi_hdl->handler_f( rpl_tree, mi_hdl, 0 );
	}
	return 0;

error:
	return  -1;
}	



struct mi_root* mi_pua_subscribe(struct mi_root* cmd, void* param)
{
	int exp= 0;
	str pres_uri, watcher_uri, expires;
	struct mi_node* node= NULL;
	struct mi_root* rpl= NULL;
	struct sip_uri uri;
	subs_info_t subs;

	node = cmd->node.kids;
	if(node == NULL)
		return 0;

	pres_uri= node->value;
	if(pres_uri.s == NULL || pres_uri.s== 0)
	{
		return init_mi_tree(400, "Bad uri", 7);
	}
	if(parse_uri(pres_uri.s, pres_uri.len, &uri)<0 )
	{
		LOG(L_ERR, "pua_mi:mi_pua_subscribe: ERROR bad uri\n");	
		return init_mi_tree(400, "Bad uri", 7);
	}

	node = node->next;
	if(node == NULL)
		return 0;

	watcher_uri= node->value;
	if(watcher_uri.s == NULL || watcher_uri.s== 0)
	{
		return init_mi_tree(400, "Bad uri", 7);
	}
	if(parse_uri(watcher_uri.s, watcher_uri.len, &uri)<0 )
	{
		LOG(L_ERR, "pua_mi:pua_mi_subscribe: ERROR bad uri\n");	
		return init_mi_tree(400, "Bad uri", 7);
	}

	node = node->next;
	if(node == NULL || node->next!=NULL)
	{
		LOG(L_ERR, "pua_mi:pua_mi_subscribe: Too much or too many"
				" parameters\n");
		return 0;
	}

	expires= node->value;
	if(expires.s== NULL || expires.len== 0)
	{
		LOG(L_ERR, "pua_mi:pua_mi_subscribe: Bad expires parameter\n");
		return init_mi_tree(400, "Bad expires", 11);
	}		

	if( str2int(&expires,(unsigned int*) &exp)< 0 )
	{
		LOG(L_ERR, "pua_mi:pua_mi_subscribe: Error while transforming str to"
				" int\n");
		return 0;
	}
	
	memset(&subs, 0, sizeof(subs_info_t));

	
	subs.pres_uri= &pres_uri;

	subs.watcher_uri= &watcher_uri;

	subs.contact= &watcher_uri;
	
	subs.expires= exp;
	subs.source_flag |= MI_SUBSCRIBE;
	subs.event= PRESENCE_EVENT;

	if(pua_send_subscribe(&subs)< 0)
	{
		LOG(L_ERR, "pua_mi:pua_mi_subscribe: ERROR while sending subscribe\n");
		goto error;
	}
	

	rpl= init_mi_tree(202, "accepted", 8);
	if(rpl == NULL)
		return 0;
	
	return rpl;

error:

	return 0;

}

