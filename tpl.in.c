static int
NS(tpl_combine)(tpl_t *, const tpl_t *, uint16_t, VAL_TYPE*, int, int);

static int
NS(tpl_combine_block)(tpl_t *out, const tpl_t *tpl,
                      uint16_t envid, VAL_TYPE *vals, int nb, int flags)
{
    int res;

    assert (tpl->op & TPL_OP_BLOCK);
    for (int i = 0; i < tpl->u.blocks.len; i++) {
        if ((res = NS(tpl_combine)(out, tpl->u.blocks.tab[i], envid, vals, nb, flags)))
            return res;
    }
    return 0;
}

static int
NS(tpl_combine_seq)(tpl_t *out, const tpl_t *tpl,
                    uint16_t envid, VAL_TYPE *vals, int nb, int flags)
{
    tpl_t *tmp2;
    int res;

    for (int i = 0; i < tpl->u.blocks.len; i++) {
        tmp2 = tpl_dup(tpl->u.blocks.tab[i]);
        if ((res = TPL_SUBST(&tmp2, envid, vals, nb, flags | TPL_KEEPVAR))) {
            e_trace(2, "cound not subst block %d", i);
            return res;
        }
        out->is_const &= tmp2->is_const;
        qv_append(&out->u.blocks, tmp2);
    }
    return 0;
}

static int
NS(tpl_combine)(tpl_t *out, const tpl_t *tpl,
                uint16_t envid, VAL_TYPE *vals, int nb, int flags)
{
    tpl_t *tmp;
    int res;

    switch (tpl->op) {
      case TPL_OP_DATA:
        if (tpl->u.data.len > 0)
            tpl_add_tpl(out, tpl);
        return 0;
      case TPL_OP_BLOB:
        if (tpl->u.blob.len > 0)
            tpl_add_tpl(out, tpl);
        return 0;

      case TPL_OP_VAR:
        if (tpl->u.varidx >> 16 == envid) {
            VAL_TYPE_P vtmp = GETVAR(tpl->u.varidx, vals, nb);
            if (!vtmp) {
                e_trace(2, "cound not find var %x (in env %d)", tpl->u.varidx,
                        envid);
                return -1;
            }
            return DEAL_WITH_VAR(out, vtmp, envid, vals, nb, flags);
        }
        tpl_add_tpl(out, tpl);
        out->is_const = false;
        return 0;

      case TPL_OP_BLOCK:
        return NS(tpl_combine_block)(out, tpl, envid, vals, nb, flags);

      case TPL_OP_SEQ:
        return NS(tpl_combine_seq)(out, tpl, envid, vals, nb, flags);

      case TPL_OP_IFDEF:
        if (tpl->u.varidx >> 16 == envid) {
            int branch = GETVAR(tpl->u.varidx, vals, nb) == NULL;
            if (tpl->u.blocks.len <= branch || !tpl->u.blocks.tab[branch])
                return 0;
            return NS(tpl_combine)(out, tpl->u.blocks.tab[branch], envid,
                                   vals, nb, flags);
        }
        out->is_const = false;
        if (tpl->is_const) {
            tpl_add_tpl(out, tpl);
            return 0;
        }
        qv_append(&out->u.blocks, tmp = tpl_new_op(TPL_OP_IFDEF));
        tmp->u.varidx = tpl->u.varidx;
        tmp->is_const = true;
        return NS(tpl_combine_seq)(tmp, tpl, envid, vals, nb, flags);

      case TPL_OP_APPLY:
      case TPL_OP_APPLY_ASSOC:
      case TPL_OP_APPLY_SEQ:
        tmp = tpl_new();
        tmp->is_const = true;
        if (tpl->op == TPL_OP_APPLY_SEQ) {
            tmp->op = TPL_OP_SEQ;
            res = NS(tpl_combine_seq)(tmp, tpl, envid, vals, nb, flags);
        } else {
            res = NS(tpl_combine_block)(tmp, tpl, envid, vals, nb, flags);
        }
        if (res) {
            tpl_delete(&tmp);
            return res;
        }
        if (tmp->is_const) {
            res = tpl_apply(tpl->u.f, out, NULL, tmp);
            tpl_delete(&tmp);
            if (res) {
                e_trace(2, "apply func %p failed", tpl->u.f);
            }
            return res;
        }
        tmp->op  = tpl->op;
        tmp->u.f = tpl->u.f;
        out->is_const = false;
        qv_append(&out->u.blocks, tmp);
        return 0;
    }

    e_trace(2, "broke from switch");
    return -1;
}

static int
NS(tpl_fold_sb)(sb_t *, const tpl_t *, uint16_t, VAL_TYPE *, int, int);

static int
NS(tpl_fold_block)(sb_t *out, const tpl_t *tpl,
                   uint16_t envid, VAL_TYPE *vals, int nb, int flags)
{
    int res;

    assert (tpl->op & TPL_OP_BLOCK);
    for (int i = 0; i < tpl->u.blocks.len; i++) {
        if (!tpl->u.blocks.tab[i])
            continue;
        res = NS(tpl_fold_sb)(out, tpl->u.blocks.tab[i], envid, vals, nb, flags);
        if (res)
            return res;
    }
    return 0;
}

static int
NS(tpl_fold_sb)(sb_t *out, const tpl_t *tpl,
                uint16_t envid, VAL_TYPE *vals, int nb, int flags)
{
    tpl_t *tmp;
    VAL_TYPE_P vtmp;
    int branch, res;

    switch (tpl->op) {
      case TPL_OP_DATA:
        sb_add(out, tpl->u.data.data, tpl->u.data.len);
        return 0;

      case TPL_OP_BLOB:
        sb_addsb(out, &tpl->u.blob);
        return 0;

      case TPL_OP_VAR:
        if (tpl->u.varidx >> 16 != envid)
            return -1;
        vtmp = GETVAR(tpl->u.varidx, vals, nb);
        if (!vtmp)
            return -1;
        return DEAL_WITH_VAR2(out, vtmp, envid, vals, nb, flags);

      case TPL_OP_BLOCK:
        return NS(tpl_fold_block)(out, tpl, envid, vals, nb, flags);

      case TPL_OP_SEQ:
        /* a SEQ must be under a APPLY_SEQ or SEQ: APPLY_SEQ recurse in
         * tpl_combine_seq, not in tpl_fold. As such, a fold(SEQ) means
         * that a SEQ is not under a APPLY_SEQ */
        assert (false);
        return -1;

      case TPL_OP_IFDEF:
        if (tpl->u.varidx >> 16 != envid)
            return -1;
        branch = GETVAR(tpl->u.varidx, vals, nb) == NULL;
        if (tpl->u.blocks.len <= branch || !tpl->u.blocks.tab[branch])
            return 0;
        return NS(tpl_fold_sb)(out, tpl->u.blocks.tab[branch], envid,
                               vals, nb, flags);

      case TPL_OP_APPLY:
      case TPL_OP_APPLY_ASSOC:
      case TPL_OP_APPLY_SEQ:
        tmp = tpl_new();
        if (tpl->op == TPL_OP_APPLY_SEQ) {
            tmp->op = TPL_OP_SEQ;
            res = NS(tpl_combine_seq)(tmp, tpl, envid, vals, nb,
                                      flags | TPL_KEEPVAR | TPL_LASTSUBST);
        } else {
            res = NS(tpl_combine_block)(tmp, tpl, envid, vals, nb,
                                        flags | TPL_KEEPVAR | TPL_LASTSUBST);
        }

        if (res) {
            tpl_delete(&tmp);
            return res;
        }
        if ((res = tpl_apply(tpl->u.f, NULL, out, tmp))) {
            tpl_delete(&tmp);
            return res;
        }
        tpl_delete(&tmp);
        return 0;
    }

    return -1;
}

#undef NS
#undef VAL_TYPE
#undef VAL_TYPE_P
#undef DEAL_WITH_VAR
#undef DEAL_WITH_VAR2
#undef TPL_SUBST
#undef GETVAR
