// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRv6 definitions
 * Copyright (C) 2020  Hiroki Shirokura, LINE Corporation
 */

#include "zebra.h"

#include "srv6.h"
#include "log.h"

DEFINE_QOBJ_TYPE(srv6_locator);
DEFINE_QOBJ_TYPE(srv6_sid_format);
DEFINE_MTYPE_STATIC(LIB, SRV6_LOCATOR, "SRV6 locator");
DEFINE_MTYPE_STATIC(LIB, SRV6_LOCATOR_CHUNK, "SRV6 locator chunk");
DEFINE_MTYPE_STATIC(LIB, SRV6_SID_FORMAT, "SRv6 SID format");
DEFINE_MTYPE_STATIC(LIB, SRV6_SID_CTX, "SRv6 SID context");

const char *seg6local_action2str_with_next_csid(uint32_t action, bool has_next_csid)
{
	switch (action) {
	case ZEBRA_SEG6_LOCAL_ACTION_END:
		return has_next_csid ? "uN" : "End";
	case ZEBRA_SEG6_LOCAL_ACTION_END_X:
		return has_next_csid ? "uA" : "End.X";
	case ZEBRA_SEG6_LOCAL_ACTION_END_T:
		return has_next_csid ? "uDT" : "End.T";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX2:
		return has_next_csid ? "uDX2" : "End.DX2";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX6:
		return has_next_csid ? "uDX6" : "End.DX6";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX4:
		return has_next_csid ? "uDX4" : "End.DX4";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT6:
		return has_next_csid ? "uDT6" : "End.DT6";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT4:
		return has_next_csid ? "uDT4" : "End.DT4";
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6:
		return has_next_csid ? "uB6" : "End.B6";
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP:
		return has_next_csid ? "uB6.Encap" : "End.B6.Encap";
	case ZEBRA_SEG6_LOCAL_ACTION_END_BM:
		return has_next_csid ? "uBM" : "End.BM";
	case ZEBRA_SEG6_LOCAL_ACTION_END_S:
		return "End.S";
	case ZEBRA_SEG6_LOCAL_ACTION_END_AS:
		return "End.AS";
	case ZEBRA_SEG6_LOCAL_ACTION_END_AM:
		return "End.AM";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT46:
		return has_next_csid ? "uDT46" : "End.DT46";
	case ZEBRA_SEG6_LOCAL_ACTION_UNSPEC:
		return "unspec";
	default:
		return "unknown";
	}
}

const char *seg6local_action2str(uint32_t action)
{
	return seg6local_action2str_with_next_csid(action, false);
}

int snprintf_seg6_segs(char *str,
		size_t size, const struct seg6_segs *segs)
{
	str[0] = '\0';
	for (size_t i = 0; i < segs->num_segs; i++) {
		char addr[INET6_ADDRSTRLEN];
		bool not_last = (i + 1) < segs->num_segs;

		inet_ntop(AF_INET6, &segs->segs[i], addr, sizeof(addr));
		strlcat(str, addr, size);
		strlcat(str, not_last ? "," : "", size);
	}
	return strlen(str);
}

static void seg6local_flavors2json(json_object *json, const struct seg6local_flavors_info *flv_info)
{
	json_object *json_flavors;

	json_flavors = json_object_new_array();
	json_object_object_add(json, "flavors", json_flavors);

	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_PSP))
		json_array_string_add(json_flavors, "psp");
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USP))
		json_array_string_add(json_flavors, "usp");
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USD))
		json_array_string_add(json_flavors, "usd");
}

void srv6_sid_structure2json(const struct seg6local_context *ctx, json_object *json)
{
	json_object_int_add(json, "blockLen", ctx->block_len);
	json_object_int_add(json, "nodeLen", ctx->node_len);
	json_object_int_add(json, "funcLen", ctx->function_len);
	json_object_int_add(json, "argLen", ctx->argument_len);
}

void seg6local_context2json(const struct seg6local_context *ctx,
			    uint32_t action, json_object *json)
{
	seg6local_flavors2json(json, &ctx->flv);
	switch (action) {
	case ZEBRA_SEG6_LOCAL_ACTION_END:
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_X:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX6:
		json_object_string_addf(json, "nh6", "%pI6", &ctx->nh6);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX4:
		json_object_string_addf(json, "nh4", "%pI4", &ctx->nh4);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_T:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT4:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT46:
		json_object_int_add(json, "table", ctx->table);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX2:
		json_object_boolean_add(json, "none", true);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP:
		json_object_string_addf(json, "nh6", "%pI6", &ctx->nh6);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_BM:
	case ZEBRA_SEG6_LOCAL_ACTION_END_S:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AS:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AM:
	case ZEBRA_SEG6_LOCAL_ACTION_UNSPEC:
	default:
		json_object_boolean_add(json, "unknown", true);
		return;
	}
}

static char *seg6local_flavors2str(char *str, size_t size,
				   const struct seg6local_flavors_info *flv_info)
{
	size_t len = 0;
	bool first = true;

	if (!CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_PSP |
							  ZEBRA_SEG6_LOCAL_FLV_OP_USP |
							  ZEBRA_SEG6_LOCAL_FLV_OP_USD))
		return str;

	len += snprintf(str + len, size - len, "(");
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_PSP)) {
		/*
		 * First is never null for this one.  If you reorder ensure
		 * that we properly touch first here in the snprintf
		 */
		len += snprintf(str + len, size - len, "%sPSP", "");
		first = false;
	}
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USP)) {
		len += snprintf(str + len, size - len, "%sUSP", first ? "" : "/");
		first = false;
	}
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USD))
		len += snprintf(str + len, size - len, "%sUSD", first ? "" : "/");

	snprintf(str + len, size - len, ")");

	return str;
}
const char *seg6local_context2str(char *str, size_t size,
				  const struct seg6local_context *ctx,
				  uint32_t action)
{
	char flavor[SRV6_FLAVORS_STRLEN], *p_flavor;

	flavor[0] = '\0';
	p_flavor = seg6local_flavors2str(flavor, sizeof(flavor), &ctx->flv);
	switch (action) {
	case ZEBRA_SEG6_LOCAL_ACTION_END:
		snprintf(str, size, "%s", p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_X:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX6:
		snprintfrr(str, size, "nh6 %pI6%s", &ctx->nh6, p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_DX4:
		snprintfrr(str, size, "nh4 %pI4%s", &ctx->nh4, p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_T:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT4:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT46:
		snprintf(str, size, "table %u%s", ctx->table, p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_B6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP:
		snprintfrr(str, size, "nh6 %pI6%s", &ctx->nh6, p_flavor);
		return str;
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX2:
	case ZEBRA_SEG6_LOCAL_ACTION_END_BM:
	case ZEBRA_SEG6_LOCAL_ACTION_END_S:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AS:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AM:
	case ZEBRA_SEG6_LOCAL_ACTION_UNSPEC:
	default:
		snprintf(str, size, "unknown(%s)", __func__);
		return str;
	}
}

void srv6_locator_chunk_list_free(void *data)
{
	struct srv6_locator_chunk *chunk = data;

	srv6_locator_chunk_free(&chunk);
}

struct srv6_locator *srv6_locator_alloc(const char *name)
{
	struct srv6_locator *locator = NULL;

	locator = XCALLOC(MTYPE_SRV6_LOCATOR, sizeof(struct srv6_locator));
	strlcpy(locator->name, name, sizeof(locator->name));
	locator->chunks = list_new();
	locator->chunks->del = srv6_locator_chunk_list_free;

	QOBJ_REG(locator, srv6_locator);
	return locator;
}

struct srv6_locator_chunk *srv6_locator_chunk_alloc(void)
{
	struct srv6_locator_chunk *chunk = NULL;

	chunk = XCALLOC(MTYPE_SRV6_LOCATOR_CHUNK,
			sizeof(struct srv6_locator_chunk));
	return chunk;
}

void srv6_locator_copy(struct srv6_locator *copy,
		       const struct srv6_locator *locator)
{
	strlcpy(copy->name, locator->name, sizeof(locator->name));
	copy->prefix = locator->prefix;
	copy->block_bits_length = locator->block_bits_length;
	copy->node_bits_length = locator->node_bits_length;
	copy->function_bits_length = locator->function_bits_length;
	copy->argument_bits_length = locator->argument_bits_length;
	copy->algonum = locator->algonum;
	copy->current = locator->current;
	copy->status_up = locator->status_up;
	copy->flags = locator->flags;
}

void srv6_locator_free(struct srv6_locator *locator)
{
	if (locator) {
		QOBJ_UNREG(locator);
		list_delete(&locator->chunks);

		XFREE(MTYPE_SRV6_LOCATOR, locator);
	}
}

void srv6_locator_chunk_free(struct srv6_locator_chunk **chunk)
{
	XFREE(MTYPE_SRV6_LOCATOR_CHUNK, *chunk);
}

struct srv6_sid_format *srv6_sid_format_alloc(const char *name)
{
	struct srv6_sid_format *format = NULL;

	format = XCALLOC(MTYPE_SRV6_SID_FORMAT, sizeof(struct srv6_sid_format));
	strlcpy(format->name, name, sizeof(format->name));

	QOBJ_REG(format, srv6_sid_format);
	return format;
}

void srv6_sid_format_free(struct srv6_sid_format *format)
{
	if (!format)
		return;

	QOBJ_UNREG(format);
	XFREE(MTYPE_SRV6_SID_FORMAT, format);
}

/**
 * Free an SRv6 SID format.
 *
 * @param val SRv6 SID format to be freed
 */
void delete_srv6_sid_format(void *val)
{
	srv6_sid_format_free((struct srv6_sid_format *)val);
}

struct srv6_sid_ctx *srv6_sid_ctx_alloc(enum seg6local_action_t behavior,
					struct in_addr *nh4,
					struct in6_addr *nh6, vrf_id_t vrf_id)
{
	struct srv6_sid_ctx *ctx = NULL;

	ctx = XCALLOC(MTYPE_SRV6_SID_CTX, sizeof(struct srv6_sid_ctx));
	ctx->behavior = behavior;
	if (nh4)
		ctx->nh4 = *nh4;
	if (nh6)
		ctx->nh6 = *nh6;
	if (vrf_id)
		ctx->vrf_id = vrf_id;

	return ctx;
}

void srv6_sid_ctx_free(struct srv6_sid_ctx *ctx)
{
	XFREE(MTYPE_SRV6_SID_CTX, ctx);
}

json_object *srv6_locator_chunk_json(const struct srv6_locator_chunk *chunk)
{
	json_object *jo_root = NULL;

	jo_root = json_object_new_object();
	json_object_string_addf(jo_root, "prefix", "%pFX", &chunk->prefix);
	json_object_string_add(jo_root, "proto",
			       zebra_route_string(chunk->proto));

	return jo_root;
}

json_object *
srv6_locator_chunk_detailed_json(const struct srv6_locator_chunk *chunk)
{
	json_object *jo_root = NULL;

	jo_root = json_object_new_object();

	/* set prefix */
	json_object_string_addf(jo_root, "prefix", "%pFX", &chunk->prefix);

	/* set block_bits_length */
	json_object_int_add(jo_root, "blockBitsLength",
			    chunk->block_bits_length);

	/* set node_bits_length */
	json_object_int_add(jo_root, "nodeBitsLength", chunk->node_bits_length);

	/* set function_bits_length */
	json_object_int_add(jo_root, "functionBitsLength",
			    chunk->function_bits_length);

	/* set argument_bits_length */
	json_object_int_add(jo_root, "argumentBitsLength",
			    chunk->argument_bits_length);

	/* set keep */
	json_object_int_add(jo_root, "keep", chunk->keep);

	/* set proto */
	json_object_string_add(jo_root, "proto",
			       zebra_route_string(chunk->proto));

	/* set instance */
	json_object_int_add(jo_root, "instance", chunk->instance);

	/* set session_id */
	json_object_int_add(jo_root, "sessionId", chunk->session_id);

	return jo_root;
}

json_object *srv6_locator_json(const struct srv6_locator *loc)
{
	struct listnode *node;
	struct srv6_locator_chunk *chunk;
	json_object *jo_root = NULL;
	json_object *jo_chunk = NULL;
	json_object *jo_chunks = NULL;

	jo_root = json_object_new_object();

	/* set name */
	json_object_string_add(jo_root, "name", loc->name);

	/* set prefix */
	json_object_string_addf(jo_root, "prefix", "%pFX", &loc->prefix);

	if (loc->sid_format) {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->sid_format->block_len);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->sid_format->node_len);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->sid_format->function_len);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->sid_format->argument_len);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (loc->sid_format->type == SRV6_SID_FORMAT_TYPE_USID)
			json_object_string_add(jo_root, "behavior", "usid");
	} else {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->block_bits_length);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->node_bits_length);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->function_bits_length);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->argument_bits_length);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (CHECK_FLAG(loc->flags, SRV6_LOCATOR_USID))
			json_object_string_add(jo_root, "behavior", "usid");
	}

	/* set status_up */
	json_object_boolean_add(jo_root, "statusUp",
				loc->status_up);

	/* set chunks */
	jo_chunks = json_object_new_array();
	json_object_object_add(jo_root, "chunks", jo_chunks);
	for (ALL_LIST_ELEMENTS_RO((struct list *)loc->chunks, node, chunk)) {
		jo_chunk = srv6_locator_chunk_json(chunk);
		json_object_array_add(jo_chunks, jo_chunk);
	}

	return jo_root;
}

json_object *srv6_locator_detailed_json(const struct srv6_locator *loc)
{
	struct listnode *node;
	struct srv6_locator_chunk *chunk;
	json_object *jo_root = NULL;
	json_object *jo_chunk = NULL;
	json_object *jo_chunks = NULL;

	jo_root = json_object_new_object();

	/* set name */
	json_object_string_add(jo_root, "name", loc->name);

	/* set prefix */
	json_object_string_addf(jo_root, "prefix", "%pFX", &loc->prefix);

	if (loc->sid_format) {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->sid_format->block_len);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->sid_format->node_len);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->sid_format->function_len);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->sid_format->argument_len);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (loc->sid_format->type == SRV6_SID_FORMAT_TYPE_USID)
			json_object_string_add(jo_root, "behavior", "usid");
	} else {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->block_bits_length);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->node_bits_length);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->function_bits_length);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->argument_bits_length);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (CHECK_FLAG(loc->flags, SRV6_LOCATOR_USID))
			json_object_string_add(jo_root, "behavior", "usid");
	}

	/* set algonum */
	json_object_int_add(jo_root, "algoNum", loc->algonum);

	/* set status_up */
	json_object_boolean_add(jo_root, "statusUp", loc->status_up);

	/* set chunks */
	jo_chunks = json_object_new_array();
	json_object_object_add(jo_root, "chunks", jo_chunks);
	for (ALL_LIST_ELEMENTS_RO((struct list *)loc->chunks, node, chunk)) {
		jo_chunk = srv6_locator_chunk_detailed_json(chunk);
		json_object_array_add(jo_chunks, jo_chunk);
	}

	return jo_root;
}

/* clang-format off */
const struct frr_yang_module_info ietf_srv6_types_info = {
	.name = "ietf-srv6-types",
	.ignore_cfg_cbs = true,
	.nodes = { { .xpath = NULL } },
};
/* clang-format on */
