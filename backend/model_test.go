package qbackend

import (
	"fmt"
	"testing"
)

type CustomModel struct {
	Model
}

func (m *CustomModel) Row(row int) interface{} {
	return fmt.Sprintf("row %d", row)
}

func (m *CustomModel) RowCount() int {
	return 3
}

func (m *CustomModel) RoleNames() []string {
	return []string{"text"}
}

var _ ModelDataSource = &CustomModel{}

// Tests
func TestModelType(t *testing.T) {
	model := &CustomModel{}
	if _, ok := ((interface{})(model)).(AnyQObject); !ok {
		t.Errorf("CustomModel does not implement AnyQObject")
	}

	if err := dummyConnection.InitObject(model); err != nil {
		t.Errorf("CustomModel object initialization failed: %s", err)
	}

	if model.ModelAPI == nil {
		t.Error("ModelAPI field not initialized during QObject initialization")
	}

	if model.ModelAPI.RoleNames[0] != "text" {
		t.Error("RoleNames not initialized during QObject initialization")
	}
}
